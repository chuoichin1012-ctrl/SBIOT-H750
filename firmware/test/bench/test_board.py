#!/usr/bin/env python3
"""Host checks for the bench helpers (no board needed): python3 stm32/firmware/test/bench/test_board.py

- board.listen() never returns while its during() callback still acts on the board, and an error
  raised in during() reaches the caller;
- tools/flash.sh refuses an unknown mode, and "bind" without a key, before it touches the board.
"""
import os
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import board  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(('ok   ' if ok else 'FAIL ') + what)
    failures += not ok


class FakeProc:
    """Stands in for the PowerShell reader: the ready marker, two lines, then the end of the window."""
    def __init__(self, *args, **kwargs):
        self.stdout = iter(['0|__ready__\n', '5|SBIOT-H750 boot 0.2.0 rsr=00460000\n', '9|boot: qspi id c84016\n'])

    def wait(self):
        return 0


def with_fake(fn):
    real = board.subprocess.Popen
    board.subprocess.Popen = FakeProc
    try:
        return fn()
    finally:
        board.subprocess.Popen = real


def test_slow_during():
    done = threading.Event()

    def slow():
        time.sleep(1.0)
        done.set()

    t0 = time.time()
    try:
        with_fake(lambda: board.listen(0.1, during=slow, during_timeout=0.2))
        raised = False
    except RuntimeError:
        raised = True
    check(raised, 'a during() still running after during_timeout raises')
    time.sleep(1.2)
    check(done.is_set() and time.time() - t0 < 5, 'and the slow callback was not killed mid-way silently (it finished)')

    flag = []
    lines = with_fake(lambda: board.listen(0.1, during=lambda: (time.sleep(0.3), flag.append(1)), during_timeout=2))
    check(flag == [1] and len(lines) == 2, 'listen waits for a during() that finishes within during_timeout')


def test_during_error():
    def boom():
        raise RuntimeError('openocd exit 1')

    try:
        with_fake(lambda: board.listen(0.1, during=boom))
        msg = None
    except RuntimeError as e:
        msg = str(e)
    check(msg is not None and 'openocd exit 1' in msg, 'an error in during() reaches the caller (%s)' % msg)


def test_flash_bad_mode():
    for args, what in ((['ap'], 'ap'), (['bind'], 'bind without a key')):
        with tempfile.TemporaryDirectory() as d:
            log = os.path.join(d, 'calls')
            cli = os.path.join(d, 'cli.sh')
            open(cli, 'w').write('#!/bin/sh\necho "$@" >> %s\n' % log)
            os.chmod(cli, 0o755)
            r = subprocess.run([os.path.join(HERE, '../../tools/flash.sh')] + args, capture_output=True, text=True,
                               env=dict(os.environ, CUBEPROG_CLI=cli))
            check(r.returncode != 0 and 'usage' in (r.stdout + r.stderr) and not os.path.exists(log),
                  'flash.sh %s: usage error, CubeProgrammer never called (rc %d)' % (what, r.returncode))


test_slow_during()
test_during_error()
test_flash_bad_mode()
print('test_board_bench: %s' % ('ok' if not failures else 'FAILED'))
sys.exit(1 if failures else 0)
