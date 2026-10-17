#!/usr/bin/env python3
"""Writes the ADC calibration (the factory record, spec §6.6 and §12.4) over the UART7 console (Plan 11a).

    python3 stm32/firmware/tools/factory_cal.py G1 O1 G2 O2

G1 O1: AI1's gain and offset in mA (I' = gain x I + offset); G2 O2: AI2's. The board must run the app with its
console on UART7 (test/bench/board.py's port) and be reachable over SWD for the reset. Steps:
1. "A G1 O1 G2 O2": the app writes both copies of the record on SPI4;
2. "C": both copies read back from flash must parse (magic, version, CRC) and hold the requested values;
3. a reset over SWD (the runtime reads the record at boot only), then "a": the AI must report "cal 1".
The production path to SPI4 is the console: the team's SWD loader programs QSPI only. Exits 0 when all hold.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'test', 'bench'))

TOL = 1e-6  # the console prints millionths


def command(g1, o1, g2, o2):
    return 'A %.6f %.6f %.6f %.6f\r' % (g1, o1, g2, o2)


def parse_c(lines):
    """[(ok, (g1, o1, g2, o2)) for copy A, copy B] from the console's C line, or None."""
    for l in lines:
        m = re.search(r'app: C A (ok|bad) (-?\d+) (-?\d+) (-?\d+) (-?\d+) B (ok|bad) (-?\d+) (-?\d+) (-?\d+) (-?\d+)', l)
        if m:
            g = m.groups()
            return [(g[0] == 'ok', tuple(int(x) / 1e6 for x in g[1:5])), (g[5] == 'ok', tuple(int(x) / 1e6 for x in g[6:10]))]
    return None


def copies_hold(copies, want):
    return bool(copies) and all(ok and all(abs(a - b) <= TOL for a, b in zip(vals, want)) for ok, vals in copies)


def calibrated(lines):
    for l in lines:
        m = re.search(r'app: a .* cal (\d)', l)
        if m:
            return m.group(1) == '1'
    return None


def main(argv):
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    want = tuple(float(x) for x in argv)
    import board
    lines = [l for _, l in board.listen(8, sends=[(0.5, command(*want)), (4.0, 'C')])]
    copies = parse_c(lines)
    if not copies_hold(copies, want):
        print('factory_cal: the record read back does not hold the values (%s)' % copies, file=sys.stderr)
        return 1
    board.listen(25, during=board.hard_reset)
    time.sleep(2)
    cal = calibrated([l for _, l in board.listen(3, sends=[(0.5, 'a')])])
    print('factory_cal: both copies hold %s; after the reset the AI report cal %s' % (want, cal))
    return 0 if cal else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
