#!/usr/bin/env python3
"""Plan 09c bench: RAM measured on the board (spec §12.3), the numbers the node-limit decision needs.

    python3 stm32/firmware/test/bench/mem_check.py

(b) the largest Edge model (100 points, both buses, 5 Modbus TCP devices; nobody answers: memory is the same);
(c) the same plus MQTT over TLS, UART1 a TLS client and UART2 an HTTPS client, each with a session up;
(d) the same plus a 48 KB edgeDevices save while a GET of it runs;
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


def largest_model():
    nodes = lambda n, base, **kw: [dict({'name': '%s%d' % (base, i), 'reg': 20 + i, 'timeout': 300}, **kw) for i in range(n)]
    devs = [ec.plc(nodes=nodes(40, 'p')), {'name': 'b2', 'addr': '9', 'uart': 2, 'poll': 2000, 'nodes': nodes(20, 'q')}]
    for k in range(5):
        devs.append(ec.tcpdev(name='tcp%d' % k, nodes=nodes(6, 'e%d_' % k)))
    devs += [ec.io(), ec.virt(nodes=[{'name': 'v%d' % i, 'formula': '=%s+1,p0', 'dtype': 'float32_abcd'} for i in range(8)])]
    tmpl = {n['name']: n['name'] for d in devs for n in d['nodes'] if 'name' in n}
    return devs, {'reporting': 1, 'interval': 5, 'template': json.dumps(tmpl)}


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
        out['c'] = heap()

        # (d) plus a 48 KB save while a GET runs
        model = json.dumps(devs, separators=(',', ':')).encode()
        big = model[:-1] + b' ' * (48 * 1024 - len(model)) + b']'
        got = {}
        g = threading.Thread(target=lambda: got.update(get=w.req('GET', '/api/config/edgeDevices')[0]))
        g.start()
        s, _, _ = w.req('POST', '/api/config/edgeDevices', body=big, timeout=60)
        g.join()
        check(s == 200 and got.get('get') == 200, 'a 48 KB save while a GET runs (%d, %s)' % (s, got.get('get')))
        out['d'] = heap()

        # (e) 50 rounds; the MQTT TLS session made again every 10 rounds
        a0 = bc.adc_pace()
        levels = []
        for r in range(50):
            if r and r % 10 == 0:
                peers[0].kill()  # the relay goes: MQTT connects again through a new one (a new TLS handshake)
                peers[0].wait()
                peers[0] = mc.relay(400, to=mc.TLSP, port=mc.TLS_RELAY, raw='1')
                time.sleep(8)
            w.req('POST', '/api/config/edgeDevices', body=big, timeout=60)
            w.req('GET', '/api/config/edgeDevices')
            if r % 10 == 9:
                levels.append(heap()['free'])
        a1 = bc.adc_pace()
        out['e'] = heap()
        wins = (a1[1] - a0[1]) / (a1[0] - a0[0]) if a0 and a1 else -1
        late = a1[2] - a0[2] if a0 and a1 else -1
        check(levels and max(levels) - min(levels) <= 4096, '50 rounds: the heap free after each 10 stays level (%s)' % levels)
        check(9.5 <= wins <= 10.5 and late == 0 and out['e']['wdog_ms'] < 250,
              'IO and watchdog kept their pace: ADC %.2f windows/s, late +%d, wdog max gap %d ms' % (wins, late, out['e']['wdog_ms']))
    finally:
        for p in peers:
            bc.wait_peer(p, 1)
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
