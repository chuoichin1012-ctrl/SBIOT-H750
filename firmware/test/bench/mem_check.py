#!/usr/bin/env python3
"""Plan 09c/09d bench: RAM measured on the board (spec §12.3), with the largest Edge model.

    python3 stm32/firmware/test/bench/mem_check.py

(b) the largest Edge model (200 points, both buses, 5 Modbus TCP devices; nobody answers: memory is the same),
    reporting over MQTT a template of 100 values (within the 26 624-byte report limit);
(c) the same plus MQTT over TLS, UART1 a TLS client and UART2 an HTTPS client, each with a session up,
    and the reports arriving at the broker;
(d) the same plus a 96 000-byte edgeDevices save while a GET of it runs;
(e) 50 rounds of upload and GET, the MQTT TLS session made again every 10 rounds: the heap comes back to its
    level, the watchdog and the ADC keep their pace.
Prints one line per check, the measurements, and "bench: ok". Ends with MQTT and Edge off.
"""
import base64
import json
import re
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_env  # noqa: E402
import board  # noqa: E402
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
            'wdog_ms': s.get('iwdgMaxGapMs', -1), 'wdi_ms': s.get('wdiMaxGapMs', -1), 'stack': '%s %s words' % (sm.get('task'), sm.get('words'))}


def last_connect_ms():
    """The uptime (ms) of MQTT's last "connected" line, -1 for none. A count of those lines would not do: the
    RAM log is a 32 KB ring, and UART2's HTTPS lines push the oldest out within about 3 minutes."""
    ms = [int(x) for x in re.findall(r'I \((\d+)\) mqtt: connected to', ec.log_text())]
    return max(ms) if ms else -1


def wait_reconnect(after_ms, secs):
    end = time.time() + secs
    while time.time() < end:
        if last_connect_ms() > after_ms:
            return True
        time.sleep(0.5)
    return False


def resting_free(reads=3):
    """The heap's resting level: the most free of a few reads 1 s apart, as UART2's HTTPS client holds a TLS
    session (about 27 KB) for part of every 1.3 s."""
    free = []
    for _ in range(reads):
        time.sleep(1)
        free.append(heap()['free'])
    return max(free)


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


REPORTED = 100  # values in the report template (Plan 09d/09e: all 200 points polled, about 100 reported)
REPORT_MAX = 26 * 1024
PER_SLOT = 2 + 6 * 40  # as edge_tmpl_size and the UI's templateSize


def largest_model():
    """200 points: 80 on UART1, 60 on UART2, 5 Modbus TCP devices of 8, 2 IO, 18 virtual (Plan 09e)."""
    nodes = lambda n, base, **kw: [dict({'name': '%s%d' % (base, i), 'reg': 20 + i, 'timeout': 300}, **kw) for i in range(n)]
    devs = [ec.plc(nodes=nodes(80, 'p')), {'name': 'b2', 'addr': '9', 'uart': 2, 'poll': 2000, 'nodes': nodes(60, 'q')}]
    for k in range(5):
        devs.append(ec.tcpdev(name='tcp%d' % k, nodes=nodes(8, 'e%d_' % k)))
    devs += [ec.io(), ec.virt(nodes=[{'name': 'v%d' % i, 'formula': '=%s+1,p0', 'dtype': 'float32_abcd'} for i in range(18)])]
    names = [n['name'] for d in devs for n in d['nodes'] if 'name' in n]
    assert sum(len(d['nodes']) for d in devs) == 200
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
        check(est.startswith('RUNNING ') and est.split()[1].endswith('/200') and reps and all(n == REPORTED for n in reps),
              'Edge runs with 200 points (%s) and its reports reach the broker, %d values each (%s)' % (est, REPORTED, reps[-3:]))
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
                c0 = last_connect_ms()
                peers[0].kill()  # the relay goes: MQTT connects again through a new one (a new TLS handshake)
                peers[0].wait()
                peers[0] = mc.relay(400, to=mc.TLSP, port=mc.TLS_RELAY, raw='1')
                reconnects += wait_reconnect(c0, 30)
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
                levels.append(resting_free())
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


# ---- scenarios A and B of spec §12.3, with their pass criteria (Plan 11b task 4) -------------------------------

def criteria(st, label, need_largest=12 * 1024):
    """The §12.3 criteria on one /api/status read; a list of what failed."""
    sy, mem = st['system'], st['mem']
    out = []
    sm = mem['stackMin']
    if sm['words'] * 4 < 1024:
        out.append('%s: stack %s has %d bytes free (< 1 KB)' % (label, sm['task'], sm['words'] * 4))
    if sy.get('heapMin', 0) < 16 * 1024:
        out.append('%s: heap minimum free %s (< 16 KB)' % (label, sy.get('heapMin')))
    if sy.get('heapLargest', 0) < need_largest:
        out.append('%s: largest free block %s (< %d)' % (label, sy.get('heapLargest'), need_largest))
    full = [x['name'] for x in mem['pools'] if x['used'] >= x['size'] and x['name'] not in bench_env.DROPPING_POOLS]
    if full:
        out.append('%s: pool full: %s' % (label, ', '.join(full)))
    return out


def status():
    _, st = w.get_json('/api/status')
    return st


def case_a():
    """Scenario A, many connections: UART1 and UART2 TCP servers with 16 clients each, MQTT over TLS, 4 WebSocket
    clients, Edge with 200 points (5 Modbus TCP devices) and a 100-slot report template."""
    tc.make_pki()
    devs, report = largest_model()
    tc.upload('mqtt', 'ca', tc.pem('ca.pem'))
    brk = mc.main_broker().start()
    relay = mc.relay(400, to=mc.TLSP, port=mc.TLS_RELAY, raw='1')
    srv = dict(workMode=3, maxClients=16, clientLimitAction=0, allowedClients='0.0.0.0/0')
    try:
        mc.post('mqtt', mc.mqtt_cfg(port=mc.TLS_RELAY, tls=2, tlsVerify=1))
        check(ec.setup(devs, report=report, port1=bc.base(listenPort=23, **srv), port2=bc.base(listenPort=24, **srv),
                       sys_extra={'websocketTarget': 1}),
              'set up A: two TCP servers of 16, MQTT TLS, the console on, the largest Edge model')
        time.sleep(10)
        boot = status()
        largest_boot = boot['system'].get('heapLargest', 0)
        check(largest_boot >= 96001, 'at boot the largest free block holds a 96 001-byte buffer (%s)' % largest_boot)
        g0 = bench_env.gates('mem A before')
        peers = [bc.peer('manyclients', host=bench_env.GW, port=p, n=16, secs=120) for p in (23, 24)]
        wss = []
        for _ in range(4):
            try:
                ws, code = w.ws_open(6432, base64.b64encode(b'admin:admin').decode())
                if code == 101:
                    wss.append(ws)
                else:
                    ws.close()
            except OSError as e:
                print('     a WebSocket failed: %s' % e)
        time.sleep(20)
        st = status()
        ports = [x['state'] for x in st['ports']]
        load = ports == ['CONNECTED(16)', 'CONNECTED(16)'] and len(wss) == 4 and st['mqtt']['state'] == 'CONNECTED'
        check(load, 'the load exists: ports %s, %d WebSockets, MQTT %s, Edge %s' % (ports, len(wss), st['mqtt']['state'], st['edge'].get('state')))
        bad = criteria(st, 'A')
        # A certificate upload under the load: its buffer comes from the heap (the 12 KB largest-block criterion).
        s_up, _ = tc.upload('mqtt', 'ca', tc.pem('ca.pem'))
        st2 = status()
        check(s_up == 200, 'a certificate upload under the load: %d' % s_up)
        bad += criteria(st2, 'A, after an upload')
        check(not bad, 'scenario A criteria: %s' % ('; '.join(bad) or 'stacks, heap, largest block, pools ok (%s)' % heap()))
        ok_g, _, why_g = bench_env.qualify('mem A', g0)
        check(ok_g, 'gates after scenario A: %s' % why_g)
        for ws in wss:
            ws.close()
        for p in peers:
            kill_events(p)
    finally:
        kill_events(relay)
        brk.stop()
        mc.post('mqtt', mc.mqtt_cfg(enabled=0, host=''))
        mc.post('edge', {'enabled': False})
        w.restart()
        time.sleep(3)


def case_b():
    """Scenario B, many TLS, as a sequence with evidence (Codex): everything configured and one restart; the
    outbox filled offline; then the MQTT TLS handshake that never gets a CONNACK, UART1's TLS client and UART2's
    HTTPS client dialling one tlsset process at once; a slow UI reader; saves until FlashDB erases (GC)."""
    tc.make_pki()
    for scope in ('mqtt', 'port1', 'port2'):
        tc.upload(scope, 'ca', tc.pem('ca.pem'))
    PM, P1, P2 = 9450, 9451, 9452
    mq = mc.mqtt_cfg(host=WIN, port=PM, tls=2, tlsVerify=1, retryInterval=3)
    p1 = bc.base(workMode=1, remoteHost=WIN, remotePort=P1, tls=2, tlsVerify=1, reconnectInterval=2, listenPort=23)
    p2 = bc.base(workMode=4, httpHost=WIN, httpPort=P2, httpMethod=0, httpPath='/q?v=', httpHeaders='', httpStripHeaders=True,
                 httpTimeout=10, tls=2, tlsVerify=1, listenPort=24)
    try:
        ok = mc.post('mqtt', mq) and mc.post('mqttPublish', [mc.topic('mem/b')]) and mc.post('edge', {'enabled': False})
        for name, extra in (('port1', p1), ('port2', p2)):
            _, cur = w.get_json('/api/config/' + name)
            cur.update(extra)
            ok = mc.post(name, cur) and ok
        _, sysc = w.get_json('/api/config/system')
        sysc.update({'ntp': 0, 'bufferOffline': 1, 'idleRestart': 0})
        ok = mc.post('system', sysc) and ok
        check(ok and w.restart(), 'set up B: MQTT TLS (UART1 published, bufferOffline), UART1 TLS, UART2 HTTPS; nobody listens yet')
        time.sleep(5)
        g0 = bench_env.gates('mem B before')
        # The outbox filled offline: UART2 streams into UART1, whose frames MQTT publishes on mem/b.
        full = False
        for _ in range(6):
            board.listen(22, sends=[(0.3, 'E2 3 20')])
            if 'outbox full' in ec.log_text():
                full = True
                break
        check(full, 'the outbox is full (its "outbox full" log line)')
        e0 = find_erases()
        t = bc.peer('tlsset', pm=PM, p1=P1, p2=P2, secs=400, cert=tc.WPKI + 'server.pem', key=tc.WPKI + 'server.key')
        slow = bc.peer('slowget', host=bench_env.GW, port=80, secs=400)
        time.sleep(1)
        bc.send_serial(b'1', port=1)  # UART1's side of the loop: UART2's HTTPS client dials on a frame (Codex r4)
        mins, e1 = [status()], e0
        for i in range(800):  # saves through the window until FlashDB erases a sector (GC), the memory read as it goes
            _, sysc = w.get_json('/api/config/system')
            sysc['hostname'] = 'memb-%d' % i
            w.req('POST', '/api/config/system', body=json.dumps(sysc).encode())
            if i % 25 == 24:
                mins.append(status())
            if i % 50 == 49:
                e1 = find_erases()
                if e0 is not None and e1 is not None and e1 > e0:
                    break
        ev = kill_events(t)
        kill_events(slow)
        iv = handshakes(ev)
        overlap = overlapping(iv, (PM, P1, P2))
        gc = e0 is not None and e1 is not None and e1 > e0
        check(overlap, 'the three handshakes overlap ([accept, end] ms by port: %s)' % iv)
        check(gc, 'FlashDB erased during the window (GC): erases %s -> %s' % (e0, e1))
        bad = [b for st in mins if st for b in criteria(st, 'B')]
        ok = not bad
        print('     scenario B: %s' % ('passed' if ok and overlap and gc else 'partial' if ok else 'failed'))
        check(ok, 'scenario B criteria: %s' % ('; '.join(bad[:3]) or 'stacks, heap, largest block, pools ok (%s)' % heap()))
        ok_g, _, why_g = bench_env.qualify('mem B', g0)
        check(ok_g, 'gates after scenario B: %s' % why_g)
    finally:
        mc.post('mqtt', mc.mqtt_cfg(enabled=0, host=''))
        mc.post('mqttPublish', [])
        _, sysc = w.get_json('/api/config/system')
        sysc.update({'bufferOffline': 0, 'hostname': 'saved-name'})
        w.req('POST', '/api/config/system', body=json.dumps(sysc).encode())
        w.restart()
        time.sleep(3)


def handshakes(ev):
    """{port: [(accept ms, end ms)]} from tlsset's events: the n-th accept on a port ends at its n-th hs or hsfail."""
    acc, end = {}, {}
    for e in ev:
        if e.get('ev') == 'accept':
            acc.setdefault(e['port'], []).append(e['t'])
        elif e.get('ev') in ('hs', 'hsfail'):
            end.setdefault(e['port'], []).append(e['t'])
    return {p: list(zip(a, sorted(end.get(p, [])))) for p, a in acc.items()}


def overlapping(iv, ports):
    """True when one instant lies inside a handshake of every port: the three were in flight at once."""
    starts = [a for p in ports for a, _ in iv.get(p, [])]
    return any(all(any(a <= t <= b for a, b in iv.get(p, [])) for p in ports) for t in starts)


def find_erases():
    m = re.search(r'erases (\d+)', ' '.join(l for _, l in board.listen(2, sends=[(0.3, 'S')])))
    return int(m.group(1)) if m else None


if __name__ == '__main__':
    arg = sys.argv[1:]
    if arg in (['a'], ['b']):
        (case_a if arg == ['a'] else case_b)()
        print('bench: %s' % ('ok' if not bc.failures else 'FAILED (%d)' % len(bc.failures)))
        sys.exit(1 if bc.failures else 0)
    sys.exit(main())
