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
   then the same from the halt loop (QSPI blanked, nothing to install);
5. a binding record of another UID: the application slot is erased (read back over SWD) and the
   board halts; then the board is bound and flashed again; an app built with another public key
   refuses the record;
6. an upload cut off: nothing is installed; a SPI4 whose init fails (forced through the debugger,
   the bus itself fine) is never used, even with a new image in staging.
Prints one line per check and "bench: ok"; exits 1 on any failure. Takes about 6 minutes.
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


def reload_in_halt(seconds):
    """Holds Reload (PA8) low for `seconds` while the bootloader sits in its halt loop."""
    board.openocd('halt; set m [read_memory 0x%08x 32 1]; '
                  'mww 0x%08x [expr {($m & ~(3 << 16)) | (1 << 16)}]; mww 0x%08x [expr {1 << 24}]; '
                  'resume; sleep %d; halt; set m [read_memory 0x%08x 32 1]; '
                  'mww 0x%08x [expr {$m & ~(3 << 16)}]; resume'
                  % (GPIOA_MODER, GPIOA_MODER, GPIOA_BSRR, int(seconds * 1000), GPIOA_MODER, GPIOA_MODER))


def spi4_init_fails(boot_elf):
    """Resets the board and makes spi_nor_init() return NOR_EIO (r0 at its return address), while
    the SPI4 bus itself keeps working."""
    fn = board.symbol(boot_elf, 'spi_nor_init') & ~1
    board.openocd('reset halt; bp 0x%08x 2 hw; resume; wait_halt 3000; rbp 0x%08x; '
                  'set ret [expr {[lindex [reg lr] 2] & ~1}]; bp $ret 2 hw; resume; wait_halt 3000; '
                  'rbp $ret; reg r0 0xfffffffe; resume' % (fn, fn))


def other_key_app(build):
    """app.img built with another public key (a second build dir), so the bootloader's dev key
    passes and the app's check fails."""
    d = tempfile.mkdtemp()
    key = os.path.join(d, 'other.pub')
    open(key, 'wb').write(os.urandom(32))
    out = os.path.join(d, 'build')
    subprocess.run(['cmake', '--preset', 'h750', '-B', out, '-DSBIOT_BIND_PUBKEY_FILE=' + key], cwd=FW,
                   check=True, capture_output=True)
    subprocess.run(['cmake', '--build', out, '--target', 'app'], cwd=FW, check=True, capture_output=True)
    return out


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

    # 3. Resets in the middle of an install. Staged without the app's reset ('U'), so the first
    # reset below starts the install: every reset is timed from the last one, never from the time
    # PowerShell needs to open the port. Spaced 1.5-2.7 s (sleep + the CLI's own latency), shorter
    # than the 3.7 s install, so every reset cuts one.
    lines = board.upload(image(build, '0.4.2', 300 * 1024), 60, reset=False)
    check('ota: done 0' in texts(lines), 'upload v0.4.2 (300 KB), not booted: ota: done 0')
    random.seed(4)

    def resets():
        board.hard_reset()
        for _ in range(5):
            time.sleep(random.uniform(0.3, 1.5))
            board.hard_reset()
    lines = board.listen(45, during=resets)
    t = texts(lines)
    done_at = t.index('boot: install installed-staging') if 'boot: install installed-staging' in t else len(t)
    cut = sum(line.startswith('SBIOT-H750 boot ') for line in t[:done_at]) - 1
    check(not any('halted' in line for line in t), 'resets mid-install: never halted')
    check(t.count('boot: install installed-staging') == 1 and cut >= 3,
          'resets mid-install: %d installs cut short, then one completed' % cut)
    check(report().get('qspi', '').startswith('v0.4.2 '), 'and v0.4.2 runs')

    # 6a. An upload cut off after two blocks: nothing installed
    lines = board.upload(image(build, '0.4.3'), 14, send=8192)
    check('ota: timeout at 8192' in texts(lines), 'upload cut off after 8 KB: the app times out')
    lines = board.listen(4, during=board.hard_reset)
    check('boot: install' in texts(lines), 'the boot after installs nothing')
    check(report().get('qspi', '').startswith('v0.4.2 '), 'and v0.4.2 still runs')

    # 6b. SPI4 init fails with a new image in staging: SPI4 is not used
    lines = board.upload(image(build, '0.4.4'), 12, reset=False)
    r = report()
    check('ota: done 0' in texts(lines) and r.get('staging', '').startswith('v0.4.4 '), 'v0.4.4 staged, not booted')
    lines = board.listen(8, during=lambda: spi4_init_fails(boot_elf))
    check(any(line.startswith('boot: spi4 id') and line.endswith('FAILED') for line in texts(lines)),
          'spi4 init forced to fail: reported')
    check('boot: install' in texts(lines) and has(lines, 'SBIOT-H750 app '), 'and not used: nothing installed, the app runs')
    check(report().get('qspi', '').startswith('v0.4.2 '), 'v0.4.2 still in QSPI')
    lines = board.listen(8, during=board.hard_reset)
    check('boot: install installed-staging' in texts(lines), 'a normal boot then installs v0.4.4')

    # 4. Reload held at power-up
    lines = board.listen(25, during=lambda: reload_held(boot_elf, 7))
    check('boot: install (reload) restored-factory' in texts(lines), 'Reload 5 s: factory image restored')
    check(has(lines, 'SBIOT-H750 app '), 'and it runs')
    lines = board.listen(5, during=board.hard_reset)
    check('boot: install' in texts(lines), 'the boot after does not install staging again')
    subprocess.run([FLASH, 'blank', build], check=True, capture_output=True)  # QSPI header gone, staging empty
    lines = board.listen(4, during=board.hard_reset)
    check(has(lines, 'boot: no valid image in QSPI, halted'), 'QSPI blanked, staging empty: halt')
    lines = board.listen(20, during=lambda: reload_in_halt(7))
    check('boot: install (reload) restored-factory' in texts(lines) and has(lines, 'SBIOT-H750 app '),
          'Reload 5 s in the halt loop: factory image restored and running')

    # 5. A record of another chip, then the board flashed and bound again
    env = dict(os.environ, SBIOT_BIND_UID='00112233445566778899aabb')
    subprocess.run([FLASH, 'bind', 'dev', build], env=env, check=True, capture_output=True)
    lines = board.listen(25, during=board.hard_reset)
    check('bind: CLONE: application erased' in texts(lines), 'record of another UID: clone, application erased')
    check(not has(lines, 'SBIOT-H750 app '), 'and the app does not run')
    words = board.qspi_read32(0x90000000, 4) + board.qspi_read32(0x90008000, 4) + board.qspi_read32(0x9016FFF0, 4)
    check(len(words) == 12 and all(w == 0xFFFFFFFF for w in words), 'QSPI read over SWD: the slot is erased (%s)'
          % ' '.join('%08x' % w for w in words[:4]))
    # The record first: flash.sh resets the board after flashing, and a boot with the old record of
    # another UID would erase the new application again (spec §12.4, the production order).
    subprocess.run([FLASH, 'bind', 'dev', build], check=True, capture_output=True)
    subprocess.run([FLASH, 'both', build], check=True, capture_output=True)
    lines = board.listen(4, during=board.hard_reset)
    check(has(lines, 'bind: ok') and has(lines, 'SBIOT-H750 app '), 'flashed and bound again: the app runs')
    other = other_key_app(build)
    subprocess.run([FLASH, 'app', other], check=True, capture_output=True)
    lines = board.listen(5, during=board.hard_reset)
    check(has(lines, 'bind: ok') and 'app: anti-clone record INVALID, nothing started' in texts(lines),
          'app built with another key: the bootloader passes, the app refuses')
    subprocess.run([FLASH, 'app', build], check=True, capture_output=True)
    lines = board.listen(4, during=board.hard_reset)
    check(has(lines, 'app: spi4 id'), 'the right app again: it starts')
    print('factory image during the run: %s' % factory)

    print('bench: %s' % ('ok' if not failures else 'FAILED (%d)' % len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
