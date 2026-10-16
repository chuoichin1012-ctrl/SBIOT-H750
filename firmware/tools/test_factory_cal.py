#!/usr/bin/env python3
"""Checks tools/factory_cal.py without a board: the command it sends and the parse of the console's answers."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import factory_cal as fc  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(('ok   ' if ok else 'FAIL ') + what)
    failures += not ok


check(fc.command(1.0, 0.0, 0.998, -0.012) == 'A 1.000000 0.000000 0.998000 -0.012000\r', 'the A line')
good = ['junk', 'app: C A ok 1000000 0 998000 -12000 B ok 1000000 0 998000 -12000']
copies = fc.parse_c(good)
check(copies == [(True, (1.0, 0.0, 0.998, -0.012)), (True, (1.0, 0.0, 0.998, -0.012))], 'two good copies parsed (%s)' % copies)
check(fc.copies_hold(copies, (1.0, 0.0, 0.998, -0.012)), 'both hold the request')
check(not fc.copies_hold(copies, (1.0, 0.0, 0.997, -0.012)), 'a value that differs from the request is refused')
bad = fc.parse_c(['app: C A ok 1000000 0 998000 -12000 B bad 0 0 0 0'])
check(not fc.copies_hold(bad, (1.0, 0.0, 0.998, -0.012)), 'a bad copy is refused')
check(fc.parse_c(['nothing here']) is None, 'no C line: None')
check(fc.calibrated(['app: a mean 1.00 2.00 ua 0 0 ok 1 1 cal 1 windows 5']) is True, 'cal 1 after the boot')
check(fc.calibrated(['app: a mean 1.00 2.00 ua 0 0 ok 1 1 cal 0 windows 5']) is False, 'cal 0')
print('test_factory_cal: %s' % ('ok' if not failures else 'FAILED'))
sys.exit(1 if failures else 0)
