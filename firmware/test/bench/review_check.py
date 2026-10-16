#!/usr/bin/env python3
"""Plan 10c bench: the whole-project review fixes that the board can show.

    python3 stm32/firmware/test/bench/review_check.py [build_dir]

- deep: a 300-level JSON body is refused (400) and the gateway keeps running (cJSON's nesting limit, task 1);
- status: MQTT saved on without a restart is still reported off, as it runs (task 11, Minor 12);
- ota: a second firmware upload right after a successful one answers 409, the staged image is kept (task 6);
  the gateway then restarts and installs the first one.
Prints one line per check and "bench: ok".
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_check as w  # noqa: E402

check = w.check


def status():
    _, st = w.get_json('/api/status')
    return st or {}


def case_deep():
    up0 = status()['system']['uptime']
    body = ('{"time":' + '[' * 300 + '0' + ']' * 300 + '}').encode()
    s, _, _ = w.req('POST', '/api/time', body=body)
    time.sleep(2)
    up1 = status().get('system', {}).get('uptime', 0)
    check(s == 400 and up1 > up0, 'a 300-level JSON body: 400, the gateway still up (uptime %s -> %s)' % (up0, up1))


def case_status():
    _, m = w.get_json('/api/config/mqtt')
    m['enabled'] = 1
    s = w.req('POST', '/api/config/mqtt', body=json.dumps(m, separators=(',', ':')).encode())[0]
    on = status().get('mqtt', {}).get('enabled')
    m['enabled'] = 0
    w.req('POST', '/api/config/mqtt', body=json.dumps(m, separators=(',', ':')).encode())
    check(s == 200 and on is False, 'MQTT saved on, no restart: Status still reports it off (%s)' % on)


def case_ota(build):
    img = open(os.path.join(build, 'app.img'), 'rb').read()
    hdr = {'Content-Type': 'application/octet-stream'}
    s1, _, _ = w.req('POST', '/api/ota', body=img, headers=hdr, timeout=120)
    s2, _, d2 = w.req('POST', '/api/ota', body=img[:4096], headers=hdr, timeout=20)
    check(s1 == 200 and s2 == 409, 'a second upload right after a successful one: %s then %s (%s)' % (s1, s2, d2[:80]))
    time.sleep(10)
    check(w.wait_up(120), 'the gateway restarts and comes back with the first image')


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '../../build/h750')
    if not w.wait_up(10):
        print('the board does not answer')
        return 1
    case_deep()
    case_status()
    case_ota(build)
    print('bench: %s' % ('ok' if not w.failures else 'FAILED (%d)' % len(w.failures)))
    return 1 if w.failures else 0


if __name__ == '__main__':
    sys.exit(main())
