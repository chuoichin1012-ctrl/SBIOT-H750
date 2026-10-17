#!/usr/bin/env python3
"""Plan 11a task 0b bench: the supervisor of the tasks.

    python3 stm32/firmware/test/bench/supervisor_check.py [hang] [long_wait]

- hang: console 'I' stops the web task for 40 s; the supervisor's 30 s for `web` runs out, the wdog stops kicking
  and IWDG1 restarts the board; the next boot logs "wdog: web stopped for ... s: restarted";
- long_wait: MQTT on, retryInterval 300, a broker that does not answer: 7 minutes without a restart (a wait that
  is work beats in slices).
Runs with network.priority 0 (Ethernet only): the LTE module stays off, no attach on the SIM (it was locked by
fast re-attach benches), and it ends there: MQTT off, priority 0 kept, so the LTE task does not try the locked SIM
again. Set the priority back by hand once the SIM works. Prints one line per check and "bench: ok".
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402
import web_check as w  # noqa: E402

check = w.check


def status():
    try:
        _, st = w.get_json('/api/status')
    except OSError:  # the web stopped (the hang case) or the board restarts
        return {}
    return st or {}


def post(name, **kw):
    _, d = w.get_json('/api/config/' + name)
    d.update(kw)
    return w.req('POST', '/api/config/' + name, body=json.dumps(d, separators=(',', ':')).encode())[0] == 200


def restart():
    w.req('POST', '/api/restart', body=b'')
    time.sleep(15)
    return w.wait_up(90)


def log_text():
    s, _, d = w.req('GET', '/api/log')
    return d.decode(errors='replace') if s == 200 else ''


def case_hang():
    up0 = status()['system']['uptime']
    t0 = time.time()
    board.listen(2, sends=[(0.5, 'I'), (0.8, 'HANG\r')])
    # the web stops for 40 s: no answer; the board restarts about 31-33 s after the 'I'
    back = False
    while time.time() - t0 < 120:
        time.sleep(3)
        st = status()
        # restarted: the uptime is well below what it would be without a reset
        if st and st.get('system', {}).get('uptime', 10 ** 9) + 20 < up0 + (time.time() - t0):
            back = True
            break
    dt = time.time() - t0
    log = log_text().split('# Since the last start', 1)[-1]
    check(back and 'wdog: web stopped for' in log and 'restarted' in log,
          "console 'I': restarted by the supervisor about %d s later, the log names web" % dt)
    check(status().get('system', {}).get('hbMissing') == 0, 'hbMissing 0')


def case_long_wait():
    ok = post('mqtt', enabled=1, host='192.0.2.1', port=1883, retryInterval=300) and restart()
    check(ok, 'set up: MQTT on, retryInterval 300, an unreachable broker')
    up0 = status()['system']['uptime']
    time.sleep(7 * 60)
    up1 = status()['system']['uptime']
    check(up1 >= up0 + 7 * 60 - 5, '7 minutes of retry waits: no restart (uptime %d -> %d)' % (up0, up1))
    post('mqtt', enabled=0)


CASES = {'hang': case_hang, 'long_wait': case_long_wait}


def main():
    if not w.wait_up(10):
        print('the board does not answer')
        return 1
    check(post('network', priority=0) and restart(), 'set up: Ethernet only (the LTE module off)')
    try:
        for name in sys.argv[1:] or CASES:
            CASES[name]()
    finally:
        post('mqtt', enabled=0)
        restart()
    print('bench: %s' % ('ok' if not w.failures else 'FAILED (%d)' % len(w.failures)))
    return 1 if w.failures else 0


if __name__ == '__main__':
    sys.exit(main())
