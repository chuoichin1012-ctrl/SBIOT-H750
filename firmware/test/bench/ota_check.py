#!/usr/bin/env python3
"""Plan 04b bench check: OTA and anti-clone on the board (spec §11.2, §11.3, §11.5).

    python3 stm32/firmware/test/bench/ota_check.py BUILD_DIR

Needs the board flashed with BUILD_DIR's boot.bin and app.img (tools/flash.sh both) and bound with
the dev key (tools/flash.sh bind dev), so that the factory slot is seeded at the first boot. Cases:
1. a plain boot writes nothing, and the factory slot holds an image;
2. an image uploaded over UART7 is installed at the next boot, and only once;
3. resets in the middle of an install (a 300 KB image, about 4 s to install): the install starts
   over at each boot and ends on the new image, never halted;
4. Reload held at power-up (PA8 driven low through the debugger): the factory image is restored and
   staging is not installed again;
5. a binding record of another UID: the application slot is erased and the board halts; then the
   board is flashed and bound again.
Prints one line per check and "bench: ok"; exits 1 on any failure. Takes about 3 minutes.
"""
import os
import random
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import board  # noqa: E402

FW = os.path.normpath(os.path.join(HERE, '../..'))
FLASH = os.path.join(FW, 'tools/flash.sh')
GPIOA_MODER, GPIOA_BSRR = 0x58020000, 0x58020018
failures = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def texts(lines):
    return [line for _, line in lines]


def has(lines, prefix):
    return any(line.startswith(prefix) for line in texts(lines))


def report():
    """The app's 'o' answer: {'qspi': 'v0.4.1 size N', 'staging': ..., 'factory': ...}."""
    out = {}
    for line in texts(board.listen(2, sends=[(0.3, 'o')])):
        for k in ('qspi', 'staging', 'factory'):
            if line.startswith('app: %s ' % k):
                out[k] = line[len('app: %s ' % k):]
    return out


def image(build, version, pad_to=None):
    """app.bin under a new header (and padded with zeros: never executed)."""
    body = open(os.path.join(build, 'app.bin'), 'rb').read()
    if pad_to:
        body += bytes(pad_to - len(body))
    d = tempfile.mkdtemp()
    src, out = os.path.join(d, 'app.bin'), os.path.join(d, 'app-%s.img' % version)
    open(src, 'wb').write(body)
    subprocess.run(['python3', os.path.join(FW, 'tools/mkimage.py'), '--version', version, src, out],
                   check=True, capture_output=True)
    return out


def reload_held(boot_elf, seconds):
    """Resets the board and holds Reload (PA8) low for `seconds` from boot_main on: a hardware
    breakpoint at boot_main (after MX_GPIO_Init), PA8 to output low (R136 limits the current), run,
    then PA8 back to input."""
    bp = board.symbol(boot_elf, 'boot_main')
    board.openocd('reset halt; bp 0x%08x 2 hw; resume; wait_halt 3000; rbp 0x%08x; '
                  'set m [read_memory 0x%08x 32 1]; '
                  'mww 0x%08x [expr {($m & ~(3 << 16)) | (1 << 16)}]; mww 0x%08x [expr {1 << 24}]; '
                  'resume; sleep %d; halt; set m [read_memory 0x%08x 32 1]; '
                  'mww 0x%08x [expr {$m & ~(3 << 16)}]; resume'
                  % (bp, bp, GPIOA_MODER, GPIOA_MODER, GPIOA_BSRR, int(seconds * 1000), GPIOA_MODER, GPIOA_MODER))


def main():
    build = sys.argv[1]
    boot_elf = os.path.join(build, 'boot.elf')

    # 1. A plain boot
    lines = board.listen(4, during=board.hard_reset)
    check(has(lines, 'bind: ok'), 'plain boot: binding ok')
    check('boot: install' in texts(lines), 'plain boot: nothing installed, seeded or restored')
    r = report()
    check(r.get('factory', '').endswith(' ok'), 'factory slot holds a valid image (%s)' % r.get('factory'))
    factory = r.get('factory', '').split(' ')[0]

    # 2. Upload and install
    lines = board.upload(image(build, '0.4.1'), 30)
    check('ota: done 0' in texts(lines), 'upload v0.4.1: ota: done 0')
    check('boot: install installed-staging' in texts(lines), 'the next boot installs it')
    check(report().get('qspi', '').startswith('v0.4.1 '), 'v0.4.1 runs')
    lines = board.listen(4, during=board.hard_reset)
    check('boot: install' in texts(lines), 'the boot after installs nothing')

    # 3. Resets in the middle of an install
    lines = board.upload(image(build, '0.4.2', 300 * 1024), 60, stop_at_done=True)
    check('ota: done 0' in texts(lines), 'upload v0.4.2 (300 KB): ota: done 0')
    random.seed(4)

    def resets():
        for _ in range(5):
            time.sleep(random.uniform(0.5, 2.5))
            board.hard_reset()
    lines = board.listen(45, during=resets)
    banners = sum(line.startswith('SBIOT-H750 boot ') for line in texts(lines))
    done = sum(line == 'boot: install installed-staging' for line in texts(lines))
    check(not has(lines, 'boot: no valid') and not any('halted' in line for line in texts(lines)),
          'resets mid-install: never halted')
    check(banners >= 5 and done == 1, 'resets mid-install: %d boots, the install completed once' % banners)
    check(report().get('qspi', '').startswith('v0.4.2 '), 'and v0.4.2 runs')

    # 4. Reload held at power-up
    lines = board.listen(25, during=lambda: reload_held(boot_elf, 7))
    check('boot: install (reload) restored-factory' in texts(lines), 'Reload 5 s: factory image restored')
    check(has(lines, 'SBIOT-H750 app '), 'and it runs')
    lines = board.listen(5, during=board.hard_reset)
    check('boot: install' in texts(lines), 'the boot after does not install staging again')

    # 5. A record of another chip, then the board flashed and bound again
    env = dict(os.environ, SBIOT_BIND_UID='00112233445566778899aabb')
    subprocess.run([FLASH, 'bind', 'dev', build], env=env, check=True, capture_output=True)
    lines = board.listen(25, during=board.hard_reset)
    check('bind: CLONE: application erased' in texts(lines), 'record of another UID: clone, application erased')
    check(not has(lines, 'SBIOT-H750 app '), 'and the app does not run')
    # The record first: flash.sh resets the board after flashing, and a boot with the old record of
    # another UID would erase the new application again (spec §12.4, the production order).
    subprocess.run([FLASH, 'bind', 'dev', build], check=True, capture_output=True)
    subprocess.run([FLASH, 'both', build], check=True, capture_output=True)
    lines = board.listen(4, during=board.hard_reset)
    check(has(lines, 'bind: ok') and has(lines, 'SBIOT-H750 app '), 'flashed and bound again: the app runs')
    print('factory image during the run: %s' % factory)

    print('bench: %s' % ('ok' if not failures else 'FAILED (%d)' % len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
