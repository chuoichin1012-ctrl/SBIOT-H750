#!/usr/bin/env python3
"""Plan 09c/09d bench: RAM measured on the board (spec §12.3), with the largest Edge model.

    python3 stm32/firmware/test/bench/mem_check.py

(b) the largest Edge model (300 points, both buses, 5 Modbus TCP devices; nobody answers: memory is the same),
    reporting over MQTT a template of 100 values (within the 26 624-byte report limit);
(c) the same plus MQTT over TLS, UART1 a TLS client and UART2 an HTTPS client, each with a session up,
    and the reports arriving at the broker;
(d) the same plus a 96 000-byte edgeDevices save while a GET of it runs;
(e) 50 rounds of upload and GET, the MQTT TLS session made again every 10 rounds: the heap comes back to its
    level, the watchdog and the ADC keep their pace.
Prints one line per check, the measurements, and "bench: ok". Ends with MQTT and Edge off.
"""
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_check as bc  # noqa: E402
import edge_check as ec  # noqa: E402
import mqtt_check as mc  # noqa: E402
import tls_check as tc  # noqa: E402
import web_check as w  # noqa: E402

check = bc.check
WIN = bc.WIN


def heap():
    _, st = w.get_json('/api/status')
    s = st['system'] if st else {}
    sm = st['mem']['stackMin'] if st else {}
    return {'free': s.get('heapFree', -1), 'min': s.get('heapMin', -1), 'largest': s.get('heapLargest', -1),
            'wdog_ms': s.get('wdiMaxGapMs', -1), 'stack': '%s %s words' % (sm.get('task'), sm.get('words'))}


def kill_events(p):
    """Ends a Windows peer now and returns its events."""
    p.kill()
    out, _ = p.communicate()
    ev = []
    for line in (out or '').splitlines():
        try:
            ev.append(json.loads(line))
        except ValueError:
            pass
    return ev


REPORTED = 100  # values in the report template (Plan 09d: all 300 points polled, about 100 reported)
REPORT_MAX = 26 * 1024
PER_SLOT = 2 + 6 * 40  # as edge_tmpl_size and the UI's templateSize


def largest_model():
    """300 points: 120 on UART1, 100 on UART2, 5 Modbus TCP devices of 12, 2 IO, 18 virtual (Plan 09d)."""
    nodes = lambda n, base, **kw: [dict({'name': '%s%d' % (base, i), 'reg': 20 + i, 'timeout': 300}, **kw) for i in range(n)]
    devs = [ec.plc(nodes=nodes(120, 'p')), {'name': 'b2', 'addr': '9', 'uart': 2, 'poll': 2000, 'nodes': nodes(100, 'q')}]
    for k in range(5):
        devs.append(ec.tcpdev(name='tcp%d' % k, nodes=nodes(12, 'e%d_' % k)))
    devs += [ec.io(), ec.virt(nodes=[{'name': 'v%d' % i, 'formula': '=%s+1,p0', 'dtype': 'float32_abcd'} for i in range(18)])]
    names = [n['name'] for d in devs for n in d['nodes'] if 'name' in n]
    assert sum(len(d['nodes']) for d in devs) == 300
    tmpl = {n: n for n in names[:REPORTED]}
    # As the UI's templateSize: the template printed without its slots, plus a slot's worst case each, plus 1.
    size = len(json.dumps({n: '' for n in tmpl}, separators=(',', ':'))) - 2 * REPORTED + REPORTED * PER_SLOT + 1
    assert size <= REPORT_MAX, size  # the gateway would turn reporting off
    return devs, {'channel': 0, 'reporting': 1, 'interval': 5, 'reportTopic': 'edge/report', 'reportQos': 0,
                  'template': json.dumps(tmpl, separators=(',', ':'))}


def reports_of(sub):
    """The reports the broker has passed on so far, each with its number of values."""
    out = []
    for p in sub.payloads('edge/report'):
        try:
            out.append(len(json.loads(p)))
        except ValueError:
            out.append(-1)
    return out


def main():
    if not w.wait_up(10):
        print('the board does not answer')
        return 1
    tc.make_pki()
    devs, report = largest_model()
    out = {}

    # (b) Edge alone
    mc.post('mqtt', mc.mqtt_cfg(enabled=0, host=''))
    check(ec.setup(devs, report=report), 'set up (b): the largest Edge model')
    time.sleep(25)
    out['b'] = heap()

    # (c) plus MQTT over TLS, a TLS client on UART1 and an HTTPS client on UART2
    for scope in ('mqtt', 'port1', 'port2'):
        tc.upload(scope, 'ca', tc.pem('ca.pem'))
    brk = mc.main_broker().start()
    sub = mc.Sub('edge/report')
    peers = [mc.relay(400, to=mc.TLSP, port=mc.TLS_RELAY, raw='1'), tc.server(secs=400),
             bc.peer('https', port=9444, secs=400, cert=tc.WPKI + 'server.pem', key=tc.WPKI + 'server.key', reply='OK')]
    try:
        mc.post('mqtt', mc.mqtt_cfg(port=mc.TLS_RELAY, tls=2, tlsVerify=1))
        p1 = bc.base(workMode=1, remoteHost=WIN, remotePort=tc.PORT, tls=2, tlsVerify=1, reconnectInterval=2, listenPort=23)
        p2 = bc.base(workMode=4, httpHost=WIN, httpPort=9444, httpMethod=0, httpPath='/q?v=', httpHeaders='', httpStripHeaders=True,
                     httpTimeout=10, tls=2, tlsVerify=1, listenPort=24)
        check(ec.setup(devs, report=report, port1=p1, port2=p2), 'set up (c): plus MQTT TLS, UART1 TLS client, UART2 HTTPS client')
        time.sleep(15)
        bc.send_serial(b'1', port=1)  # UART1 -> UART2: UART2's HTTPS request
        time.sleep(10)
        _, st = w.get_json('/api/status')
        mq = st['mqtt']['state'] if st else ''
        ports = [p['state'] for p in st['ports']] if st else []
        check(mq == 'CONNECTED' and ports and ports[0].startswith('CONNECTED'), 'TLS sessions up: MQTT %s, UART1 %s' % (mq, ports[:1]))
        ed = st['edge'] if st else {}
        reps = reports_of(sub)
        est = str(ed.get('state', ''))
        check(est.startswith('RUNNING ') and est.split()[1].endswith('/300') and reps and all(n == REPORTED for n in reps),
              'Edge runs with 300 points (%s) and its reports reach the broker, %d values each (%s)' % (est, REPORTED, reps[-3:]))
        out['c'] = heap()

        # (d) plus a 96 000-byte save while a GET runs
        model = json.dumps(devs, separators=(',', ':')).encode()
        big = model[:-1] + b' ' * (96000 - len(model)) + b']'
        got = {}
        g = threading.Thread(target=lambda: got.update(get=w.req('GET', '/api/config/edgeDevices')[0]))
        g.start()
        s, _, _ = w.req('POST', '/api/config/edgeDevices', body=big, timeout=60)
        g.join()
        check(s == 200 and got.get('get') == 200, 'a 96 000-byte save while a GET runs (%d, %s)' % (s, got.get('get')))
        out['d'] = heap()

        # (e) 50 rounds; the MQTT TLS session made again every 10 rounds
        a0 = bc.adc_pace()
        r0, t0 = len(reports_of(sub)), time.time()
        levels, bad, reconnects, https_sent, blips = [], 0, 0, 0, 0
        for r in range(50):
            if r and r % 10 == 0:
                n0 = mc.connects()
                peers[0].kill()  # the relay goes: MQTT connects again through a new one (a new TLS handshake)
                peers[0].wait()
                peers[0] = mc.relay(400, to=mc.TLSP, port=mc.TLS_RELAY, raw='1')
                reconnects += mc.wait_connects(n0 + 1, 30)
                bc.send_serial(b'1', port=1)  # and UART2's HTTPS request, under the same load
                https_sent += 1
            sock, again = w.connect_retry(60)
            s1, _, _ = w.req('POST', '/api/config/edgeDevices', body=big, timeout=60, sock=sock)
            blips += again
            sock, again = w.connect_retry()
            s2, _, back = w.req('GET', '/api/config/edgeDevices', sock=sock)
            blips += again
            bad += not (s1 == 200 and s2 == 200 and back == big)
            if r % 10 == 9:
                levels.append(heap()['free'])
        check(bad == 0, '50 rounds: every save 200, every GET 200 with the bytes saved (%d failed; %d connects tried again)' % (bad, blips))
        check(reconnects == 4, 'MQTT over TLS connected again after each of the 4 relay restarts (%d)' % reconnects)
        secs, during = time.time() - t0, reports_of(sub)[r0:]
        check(len(during) >= secs / 5 / 2 and all(n == REPORTED for n in during),
              'the reports kept arriving under the load: %d in %.0f s, every 5 s, 4 reconnects' % (len(during), secs))
        reqs = [e for e in kill_events(peers[2]) if e.get('ev') == 'request']
        peers[2] = None
        check(len(reqs) >= 1 + https_sent, 'UART2 made its HTTPS requests under the load (%d of %d)' % (len(reqs), 1 + https_sent))
        a1 = bc.adc_pace()
        out['e'] = heap()
        wins = (a1[1] - a0[1]) / (a1[0] - a0[0]) if a0 and a1 else -1
        late = a1[2] - a0[2] if a0 and a1 else -1
        check(levels and max(levels) - min(levels) <= 4096, '50 rounds: the heap free after each 10 stays level (%s)' % levels)
        check(9.5 <= wins <= 10.5 and late == 0 and out['e']['wdog_ms'] < 250,
              'IO and watchdog kept their pace: ADC %.2f windows/s, late +%d, wdog max gap %d ms' % (wins, late, out['e']['wdog_ms']))
    finally:
        for p in peers:
            if p:
                kill_events(p)
        sub.stop()
        brk.stop()
        mc.post('mqtt', mc.mqtt_cfg(enabled=0, host=''))
        w.req('POST', '/api/config/edge', body=b'{"enabled":false}')
        w.req('POST', '/api/config/edgeDevices', body=b'[]')
    for k in ('b', 'c', 'd', 'e'):
        if k in out:
            m = out[k]
            print('     measure (%s): heap free %d, lowest %d, largest block %d; lowest stack %s' % (k, m['free'], m['min'], m['largest'], m['stack']))
    print('bench: %s' % ('ok' if not bc.failures else 'FAILED (%d)' % len(bc.failures)))
    return 1 if bc.failures else 0


if __name__ == '__main__':
    sys.exit(main())
