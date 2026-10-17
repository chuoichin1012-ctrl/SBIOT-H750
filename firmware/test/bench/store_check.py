#!/usr/bin/env python3
"""Plan 05b bench check: storage on the board (spec §9.1, §5.4, §5.5).

    python3 stm32/firmware/test/bench/store_check.py

Needs the board running this build's app (installed over UART7: board.upload). Cases:
1. the boot counter goes up with every boot, and a saved config is there after a reset;
2. 1000 saves in a row (about 300 KB, so the cfg database collects garbage): none fails, and while
   they run the CPU still idles, the watchdog task keeps its 50 ms (Plan 11a), no DMA transfer fails;
3. resets at random moments of that stress: every boot mounts, the store stays writable, and the
   config read back is one that was saved;
4. a warning reaches the persistent log and is still there after a reset.
Prints one line per check, the measurements, and "bench: ok"; exits 1 on any failure.
"""
import os
import random
import re
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402
import bench_env  # noqa: E402  (Plan 11b: the gates per workload)

failures = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def texts(lines):
    return [line for _, line in lines]


def find(lines, pattern):
    for line in texts(lines):
        m = re.search(pattern, line)
        if m:
            return m
    return None


def status(extra=()):
    return board.listen(2.5, sends=[(0.3, 'S')] + list(extra))


def main():
    # 1. Boot counter and persistence
    a = find(status(), r'store boot (\d+)')
    lines = board.listen(4, sends=[(0.3, 'p')])
    saved = find(lines, r'saved (bench-\d+): 0 in (\d+) ms')
    check(saved is not None, 'a config save answers 0 (%s ms)' % (saved.group(2) if saved else '-'))
    lines = board.listen(6, sends=[(3.5, 'S')], during=board.hard_reset)
    b = find(lines, r'store boot (\d+)')
    mount = find(lines, r'store mounted in (\d+) ms')
    check(a and b and int(b.group(1)) == int(a.group(1)) + 1, 'the boot counter goes up by one')
    check(saved and find(lines, r'"hostname":"%s"' % saved.group(1)) is not None, 'the saved config is there after a reset')
    print('     store mount at boot: %s ms' % (mount.group(1) if mount else '-'))

    # 2. 1000 saves with garbage collection: CPU, watchdog, DMA
    g0 = bench_env.gates('store 1000 saves before')
    lines = board.listen(60, sends=[(0.3, 'G'), (50.0, 'i'), (50.5, 'S')])
    st = find(lines, r'stress 1000 saves, (\d+) failed, slowest (\d+) us, (\d+) ms, idle (\d+) permille')
    gap = find(lines, r'wdog_max_gap_us=(\d+)')
    dma = find(lines, r'dma errors (\d+)')
    heap = find(lines, r'heap free \d+ min (\d+)')
    check(st is not None and st.group(1) == '0', 'stress: 1000 saves, none failed')
    if st:
        idle = int(st.group(4)) / 10.0
        print('     1000 saves in %s ms; CPU idle meanwhile: %.1f %%' % (st.group(3), idle))
        check(idle > 30, 'the CPU still idles while saving (%.1f %%)' % idle)
    check(gap is not None and int(gap.group(1)) <= 260000, 'watchdog task kept its period (max gap %s us)' % (gap.group(1) if gap else '-'))
    check(dma is not None and dma.group(1) == '0', 'no DMA transfer failed')
    ok_g, _, why_g = bench_env.qualify('store 1000 saves', g0)
    check(ok_g, 'gates after the 1000 saves (FlashDB GC): %s' % why_g)
    print('     slowest save %s us, lowest free heap %s bytes' % (st.group(2) if st else '-', heap.group(1) if heap else '-'))

    # 3. Resets in the middle of the stress
    random.seed(5)
    bad = 0
    for i in range(5):
        delay = random.uniform(1, 10)  # the stress takes about 12 s: every reset cuts it

        def stress_then_reset():
            time.sleep(delay)
            board.hard_reset()
        lines = board.listen(delay + 6, sends=[(0.3, 'G'), (delay + 4.5, 'S')], during=stress_then_reset)
        host = find(lines, r'"hostname":"([^"]*)"')
        mounted = find(lines, r'store mounted in (\d+) ms')
        ok = mounted is not None and host is not None and re.fullmatch(r'(stress-\d+|bench-\d+)', host.group(1)) is not None
        w = board.listen(3, sends=[(0.3, 'p')])
        ok = ok and find(w, r'saved bench-\d+: 0 ') is not None
        print('     reset %d after %.1f s: hostname %s, mount %s ms' % (i + 1, delay, host.group(1) if host else '-',
                                                                      mounted.group(1) if mounted else '-'))
        bad += not ok
    check(bad == 0, 'resets during saves: every boot mounts, stays writable, reads back a saved config')

    # 5. SPI4 init forced to fail in the app (r0 at spi_nor_init's return): the store does not
    # mount, so FlashDB never touches the flash; the next boot finds everything as it was.
    # Skipped while the SIM is locked (Plan 11b, Codex): without the store the app runs the defaults, network
    # priority 1, and the LTE task attaches. SBIOT_STORE_CASE5=1 runs it.
    if os.environ.get('SBIOT_STORE_CASE5') == '1':
        elf = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../build/h750/app.elf')
        before = find(status(), r'store boot (\d+)')
        host0 = find(board.listen(2, sends=[(0.3, 'S')]), r'"hostname":"([^"]*)"')
        fn = board.symbol(elf, 'spi_nor_init') & ~1
        lines = board.listen(8, during=lambda: board.openocd(
            'reset halt; bp 0x%08x 2 hw; resume; wait_halt 5000; rbp 0x%08x; '
            'set ret [expr {[lindex [reg lr] 2] & ~1}]; bp $ret 2 hw; resume; wait_halt 3000; '
            'rbp $ret; reg r0 0xfffffffe; resume' % (fn, fn)))
        check(find(lines, r'app: spi4 id \w+ FAILED') is not None and find(lines, r'app: store FAILED') is not None,
              'spi4 init forced to fail: the store does not mount')
        lines = board.listen(6, sends=[(3.5, 'S')], during=board.hard_reset)
        after = find(lines, r'store boot (\d+)')
        host1 = find(lines, r'"hostname":"([^"]*)"')
        check(before and after and int(after.group(1)) == int(before.group(1)) + 1 and host0 and host1 and host0.group(1) == host1.group(1),
              'the next boot finds the store as it was (boot %s -> %s, no write in between)' % (before.group(1) if before else '-',
                                                                                               after.group(1) if after else '-'))
    else:
        print('skip  spi4 init forced to fail (the defaults would wake LTE; SBIOT_STORE_CASE5=1 runs it)')

    # 4. The persistent log
    board.listen(2, sends=[(0.3, 'W')])
    lines = board.listen(7, during=board.hard_reset)
    boot = find(lines, r'store mounted in \d+ ms, boot (\d+)')
    prev = int(boot.group(1)) - 1 if boot else -1
    # Read over the web (Plan 11b): the console's 'l' walks the whole log from its oldest line at 115200 baud, and a
    # long-used board's log takes longer than any listen window to reach the newest.
    import web_check as w
    w.wait_up(30)
    _, _, log = w.req('GET', '/api/log')
    check(re.search(r'\[boot %d \+[0-9:]+\] W \(\d+\) bench: a warning to keep' % prev, log.decode('utf-8', 'replace')) is not None,
          'a warning of the boot before is in the persistent log')

    print('bench: %s' % ('ok' if not failures else 'FAILED (%d)' % len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
