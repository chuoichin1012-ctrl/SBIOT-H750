#!/usr/bin/env python3
"""Plan 02 bench check: cold and warm boots of the bootloader and the XIP app (spec §6.2, §11.4).

    python3 stm32/firmware/test/bench/boot_check.py BUILD_DIR [--cold]

Needs the board flashed with BUILD_DIR's boot.bin and app.img (tools/flash.sh both). With --cold it
waits up to 60 s for the user to power-cycle the board instead of running the warm-boot cases.
Prints one line per check and "bench: ok"; exits 1 on any failure.
"""
import os
import re
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402

RSR_PIN, RSR_POR, RSR_SFT, RSR_IWDG1 = 1 << 22, 1 << 23, 1 << 24, 1 << 26
failures = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def boots(lines):
    """[(ms, app_rsr)] for each complete boot: boot banner, QSPI id, jump, app banner, handoff, ITCM."""
    out = []
    want = None
    for ms, line in lines:
        if line.startswith('SBIOT-H750 boot '):
            want = ['boot: qspi id c84016', 'boot: jump msp 20020000', 'SBIOT-H750 app ',
                    'app: handoff ok (00), mpu regions 0-1 match boot', 'app: itcm vTaskSwitchContext=0x']
            start, rsr = ms, None
        elif want and line.startswith(want[0]):
            if want[0] == 'SBIOT-H750 app ':
                rsr = int(re.search(r'rsr=([0-9a-f]{8})', line).group(1), 16)
            want.pop(0)
            if not want:
                out.append((start, rsr))
                want = None
    return out


def infos(lines):
    out = []
    for _, line in lines:
        m = re.match(r'app: uptime_ms=(\d+) ticks=(\d+) wdog_max_gap_us=(\d+)', line)
        if m:
            out.append(tuple(int(x) for x in m.groups()))
    return out


def main():
    build = sys.argv[1]
    app_bin = open(os.path.join(build, 'app.img'), 'rb').read()  # the image with its header
    if '--hostile' in sys.argv:
        # Review Focus 1: a reset while the flash is in continuous-read mode or busy erasing.
        boot_elf = os.path.join(build, 'boot.elf')
        for proc, what in [('qspi_hostile_continuous', 'continuous-read mode'), ('qspi_hostile_erase', 'a running sector erase')]:
            for n in range(3):
                out = []

                def run(proc=proc):  # runs in board.listen's thread: keep an error as text, never lose it
                    try:
                        out.append(board.openocd(proc, boot_elf))
                    except Exception as e:  # noqa: BLE001
                        out.append('ERROR %s' % e)

                b = boots(board.listen(6, during=run))
                note = [l.strip() for l in ''.join(out).splitlines() if l.strip().startswith(('hostile:', 'ERROR'))]
                ok = len(b) == 1 and len(note) == 1 and note[0].startswith('hostile:')
                check(ok, 'reset during %s %d: one complete boot (%s)' % (what, n + 1, note))
    elif '--cold' in sys.argv:
        wait = float(os.environ.get('SBIOT_COLD_WAIT', '60'))  # seconds the user has to power-cycle
        print('power-cycle the board now (%d s)...' % wait, flush=True)
        b = boots(board.listen(wait))
        check(len(b) >= 1, 'cold boot reaches the app')
        check(bool(b) and bool(b[0][1] & RSR_POR), 'cold boot: PORRSTF set (rsr=%08x)' % (b[0][1] if b else 0))
    else:
        b = boots(board.listen(5, during=board.hard_reset))
        check(len(b) == 1 and bool(b[0][1] & RSR_PIN), 'pin reset: one complete boot with PINRSTF')

        lines = board.listen(4.5, sends=[(3.0, 'i'), (3.5, 'c')])
        i = infos(lines)
        check(len(i) == 1 and abs(i[0][0] - i[0][1]) <= 2, 'now_us (TIM2) agrees with the FreeRTOS tick: %s' % i)
        check(len(i) == 1 and 199000 <= i[0][2] <= 201000, 'wdog kicks every 200 ms')
        crc = [l for _, l in lines if l.startswith('app: image crc32=')]
        want = 'app: image crc32=%08x len=%d' % (zlib.crc32(app_bin), len(app_bin))
        check(len(crc) == 1 and crc[0].startswith(want), 'XIP image reads back as app.img (%s)' % (crc[0] if crc else '-'))

        resets = 10
        # 3 s apart: a boot takes about 2.3 s once the store mounts a well-used cfg database (Plan 05b)
        b = boots(board.listen(1.0 + 3 * resets, sends=[(0.5 + 3 * n, 'r') for n in range(resets)]))
        check(len(b) == resets and all(r & RSR_SFT for _, r in b),
              '%d software resets: %d complete boots with SFTRSTF' % (resets, len(b)))

        for n in range(3):
            lines = board.listen(4, sends=[(0.5, 'w')])
            stop = [ms for ms, l in lines if l.startswith('app: watchdog kicks stopped')]
            b = boots(lines)
            dt = b[0][0] - stop[0] if stop and b else -1
            check(len(b) == 1 and bool(b[0][1] & RSR_IWDG1) and 1300 <= dt <= 2000,
                  'IWDG1 reset %d: %d ms after the last kick, IWDG1RSTF set' % (n + 1, dt))
    print('bench: ok' if not failures else 'bench: %d failure(s)' % len(failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
