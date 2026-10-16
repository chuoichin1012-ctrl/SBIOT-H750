#!/usr/bin/env python3
"""Plan 06b bench check: the web server, the API and the WebSocket console on the board (spec §10).

    python3 stm32/firmware/test/bench/web_check.py BUILD_DIR

Runs from WSL, which reaches the board (192.168.0.7, factory login) through Windows' NAT. Needs the
board running BUILD_DIR's app. Cases:
1. auth: 401 without WWW-Authenticate, a wrong login, a good one; the UI gzip with ETag and 304;
2. body limits: 10 241 bytes -> 413; 13 KB with Content-Length -> 413 and chunked -> 411 (above
   MG_MAX_RECV_SIZE, so an answer and not a reset); a chunked edgeDevices -> 411; a body that stops
   -> 408 after about 30 s;
3. every config saved back as read and read again; system.hostname saved but /api/device keeps the
   running one until a restart (spec §9.1);
4. edgeDevices: a model sent as a 40 KB body (streamed) reads back; 49 153 bytes -> 413;
5. certificates: a PEM stored, a key refused as ca; /api/io answers the IO state (Plan 07b) and refuses "ao"; an unknown /api/x 404;
6. the clock: POST /api/time, then system.time and a dated log line;
7. slow readers: clients that never read (on app.js, on the WebSocket log) leave the heap level
   while another client is served;
8. the WebSocket console (LOG mode, set up for the test and restarted): a wrong login 401, help and
   status answered, live log lines, a fifth client refused;
9. OTA over the web: BUILD_DIR's app.img uploaded, installed, running.
Prints one line per check and "bench: ok"; exits 1 on any failure.
"""
import base64
import http.client
import json
import os
import re
import socket
import struct
import sys
import time

HOST, PORT = '192.168.0.7', 80
AUTH = 'Basic ' + base64.b64encode(b'admin:admin').decode()
failures = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def req(method, path, body=None, auth=AUTH, headers=None, timeout=20):
    c = http.client.HTTPConnection(HOST, PORT, timeout=timeout)
    h = dict(headers or {})
    if auth:
        h['Authorization'] = auth
    if body is not None and 'Content-Type' not in h:
        h['Content-Type'] = 'application/json'
    c.request(method, path, body=body, headers=h)
    r = c.getresponse()
    data = r.read()
    c.close()
    return r.status, dict((k.lower(), v) for k, v in r.getheaders()), data


def get_json(path):
    s, _, d = req('GET', path)
    return s, json.loads(d) if s == 200 else None


def raw(payload, wait=3.0):
    """Sends bytes as they are; returns what came back within `wait` seconds of silence."""
    s = socket.create_connection((HOST, PORT), timeout=wait)
    s.sendall(payload)
    out = b''
    try:
        while True:
            d = s.recv(4096)
            if not d:
                break
            out += d
    except socket.timeout:
        pass
    s.close()
    return out


def status_of(resp):
    m = re.match(rb'HTTP/1\.1 (\d+)', resp)
    return int(m.group(1)) if m else 0


def heap_free():
    _, st = get_json('/api/status')
    return st['mem']['heap']['free'] if st else -1


def wait_up(seconds=40):
    end = time.time() + seconds
    while time.time() < end:
        try:
            s, _, _ = req('GET', '/api/device', timeout=2)
            if s == 200:
                return True
        except OSError:
            pass
        time.sleep(1)
    return False


def restart():
    req('POST', '/api/restart', body=b'')
    time.sleep(3)
    return wait_up()


# ---- a minimal WebSocket client ---------------------------------------------------------------

def ws_open(port, token, timeout=5):
    s = socket.create_connection((HOST, port), timeout=timeout)
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall(('GET /ws?auth=%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
               'Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n' % (token, HOST, key)).encode())
    head = b''
    while b'\r\n\r\n' not in head:
        d = s.recv(1)
        if not d:
            break
        head += d
    return s, status_of(head)


def ws_send(s, text):
    data = text.encode()
    mask = os.urandom(4)
    hdr = bytes([0x81, 0x80 | len(data)]) if len(data) < 126 else bytes([0x81, 0xFE]) + struct.pack('>H', len(data))
    s.sendall(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))


def ws_recv_all(s, seconds):
    """Text of every frame received within `seconds`."""
    s.settimeout(0.3)
    buf, out, end = b'', [], time.time() + seconds
    while time.time() < end:
        try:
            d = s.recv(4096)
            if not d:
                break
            buf += d
        except socket.timeout:
            pass
        while len(buf) >= 2:
            n = buf[1] & 0x7F
            off = 2
            if n == 126:
                if len(buf) < 4:
                    break
                n = struct.unpack('>H', buf[2:4])[0]
                off = 4
            if len(buf) < off + n:
                break
            out.append(buf[off:off + n].decode(errors='replace'))
            buf = buf[off + n:]
    return out


# ---- the cases ---------------------------------------------------------------------------------

def case_auth():
    s, h, _ = req('GET', '/api/device', auth=None)
    check(s == 401 and 'www-authenticate' not in h, 'no login: 401 without WWW-Authenticate')
    s, _, _ = req('GET', '/api/device', auth='Basic ' + base64.b64encode(b'admin:wrong').decode())
    check(s == 401, 'a wrong login: 401')
    s, d = get_json('/api/device')
    check(s == 200 and d['model'] == 'SBIOT-H750' and d['portmax'] == 2, 'a good login: the device')
    s, h, body = req('GET', '/', auth=None)
    check(s == 200 and h.get('content-encoding') == 'gzip' and body[:2] == b'\x1f\x8b', 'the UI: gzip, no login')
    s, _, _ = req('GET', '/', auth=None, headers={'If-None-Match': h.get('etag', '')})
    check(s == 304, 'the UI: 304 for its ETag')


def case_limits():
    s, _, _ = req('POST', '/api/config/system', body=b' ' * 10241)
    check(s == 413, '10 241-byte body: 413 (got %d)' % s)
    big = b'{"hostname":"x"}' + b' ' * (13 * 1024)
    r = raw(b'POST /api/config/system HTTP/1.1\r\nHost: x\r\nAuthorization: ' + AUTH.encode() +
            b'\r\nContent-Length: %d\r\n\r\n' % len(big) + big)
    check(status_of(r) == 413, '13 KB with Content-Length: 413 (got %d)' % status_of(r))
    r = raw(b'POST /api/config/system HTTP/1.1\r\nHost: x\r\nAuthorization: ' + AUTH.encode() +
            b'\r\nTransfer-Encoding: chunked\r\n\r\n3400\r\n' + big[:0x3400] + b'\r\n0\r\n\r\n')
    check(status_of(r) == 411, '13 KB chunked: 411 (got %d)' % status_of(r))
    r = raw(b'POST /api/config/edgeDevices HTTP/1.1\r\nHost: x\r\nAuthorization: ' + AUTH.encode() +
            b'\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n[]\r\n0\r\n\r\n')
    check(status_of(r) == 411, 'a chunked edgeDevices: 411 (got %d)' % status_of(r))
    # A body that stops, streamed and plain: 408 once 30 s pass without a byte (the board logs the
    # silence it measured; the client's own timing includes when it notices the close).
    for path in (b'/api/config/edgeDevices', b'/api/config/system'):
        r = raw(b'POST ' + path + b' HTTP/1.1\r\nHost: x\r\nAuthorization: ' + AUTH.encode() +
                b'\r\nContent-Length: 100\r\n\r\n[{"na', wait=40)
        check(status_of(r) == 408, '%s, a body that stops: 408 (got %d)' % (path.decode(), status_of(r)))
    _, _, log = req('GET', '/api/log')
    silent = [int(x) for x in re.findall(rb'stopped arriving \((\d+) ms silent\)', log)][-2:]
    check(len(silent) == 2 and all(30000 <= x < 31000 for x in silent), 'both after 30 s of silence (%s ms)' % silent)


def case_configs():
    s, d = get_json('/api/config/system')
    names = ['network', 'port1', 'port2', 'mqtt', 'mqttPublish', 'mqttSubscribe', 'system', 'io', 'edge',
             'edgeReport', 'edgeLinkage']
    bad = []
    for n in names:
        s1, a = get_json('/api/config/' + n)
        s2, _, _ = req('POST', '/api/config/' + n, body=json.dumps(a).encode())
        s3, b = get_json('/api/config/' + n)
        if not (s1 == s2 == s3 == 200 and a == b):
            bad.append('%s (%d %d %d)' % (n, s1, s2, s3))
    check(not bad, 'every config saved back and read again%s' % (': ' + ', '.join(bad) if bad else ''))
    s, _, body = req('GET', '/api/config/nope')
    check(s == 404, 'an unknown config: 404')
    _, dev = get_json('/api/device')
    running = dev['hostname']
    sysc = dict(d, hostname='saved-name')  # the whole config, as the UI sends it: a body replaces it (Plan 09b)
    s, _, _ = req('POST', '/api/config/system', body=json.dumps(sysc, separators=(',', ':')).encode())
    _, sysc = get_json('/api/config/system')
    _, dev2 = get_json('/api/device')
    check(s == 200 and sysc['hostname'] == 'saved-name' and dev2['hostname'] == running,
          'hostname saved, /api/device keeps the running "%s" until a restart' % running)
    return running


def log_since_start():
    s, _, d = req('GET', '/api/log')
    return d.decode(errors='replace').split('# Since the last start', 1)[-1] if s == 200 else ''


def heap_free():
    _, st = get_json('/api/status')
    return st['system']['heapFree'] if st else -1


def case_heap_loop(rounds=200):
    """Plan 09c: every API endpoint, many times; the heap returns to its level (no forgotten free)."""
    _, sysc = get_json('/api/config/system')
    sysb = json.dumps(sysc, separators=(',', ':')).encode()
    get_json('/api/status')
    time.sleep(1)
    before = heap_free()
    wrong, blips = 0, 0

    def st(method, path, body=None, headers=None):
        # A connect that times out is tried once more, 2 s later: on this bench (WSL NAT) one in a few
        # thousand connects times out while the board answers the next one; a board that hangs fails both.
        nonlocal blips
        try:
            return req(method, path, body=body, headers=headers, timeout=5)[0]
        except (socket.timeout, TimeoutError):
            blips += 1
            time.sleep(2)
            return req(method, path, body=body, headers=headers)[0]

    for r in range(rounds):
        got = [st('GET', '/api/status'), st('GET', '/api/io'), st('GET', '/api/device'),
               st('POST', '/api/io', body=b'{"do":9,"value":true}'),        # refused: no DO10
               st('POST', '/api/time', body=b'{"time":"not a date"}'),      # refused
               st('POST', '/api/config/system', body=sysb),
               st('POST', '/api/config/system', body=b'{"x":'),            # bad JSON
               st('GET', '/api/config/port1'), st('GET', '/api/config/edgeDevices')]
        want = [200, 200, 200, 400, 400, 200, 400, 200, 200]
        if r % 10 == 0:  # the board closes an upload's connection itself: a TIME_WAIT each, so not every round
            got.append(st('POST', '/api/cert?scope=mqtt&kind=ca', body=b'not a pem', headers={'Content-Type': 'application/octet-stream'}))
            want.append(400)
        wrong += got != want
    time.sleep(2)
    after = heap_free()
    check(wrong == 0, 'every endpoint answered as it should, %d rounds (%d rounds otherwise; %d connects tried again)' % (rounds, wrong, blips))
    check(before > 0 and abs(after - before) <= 2048, 'heap after %d rounds of every endpoint: %d -> %d bytes free' % (rounds, before, after))


def case_edge_stream():
    """Plan 09c: an edgeDevices body streams into the records; a client that goes away commits nothing."""
    small = json.dumps(edge_model(2, 2)).encode()
    s, _, _ = req('POST', '/api/config/edgeDevices', body=small)
    big = json.dumps(edge_model(10, 10)).encode()
    big = big[:-1] + b' ' * (40 * 1024 - len(big)) + b']'
    head = ('POST /api/config/edgeDevices HTTP/1.1\r\nHost: x\r\nAuthorization: %s\r\nContent-Type: application/json\r\n'
            'Content-Length: %d\r\n\r\n' % (AUTH, len(big))).encode()
    a = socket.create_connection((HOST, PORT), 5)
    a.sendall(head + big[:20000])
    time.sleep(0.5)
    a.close()  # gone halfway
    time.sleep(1)
    s2, _, now = req('GET', '/api/config/edgeDevices')
    check(s == 200 and s2 == 200 and now == small, 'a client that closes halfway: the committed model is unchanged')
    a = socket.create_connection((HOST, PORT), 5)
    a.sendall(head + big[:20000])
    time.sleep(0.3)
    sb, _, _ = req('POST', '/api/config/edgeDevices', body=small)  # B, while A is open
    a.sendall(big[20000:])
    a.settimeout(10)
    reply = b''
    try:
        while b'\r\n\r\n' not in reply:
            d = a.recv(4096)
            if not d:
                break
            reply += d
    except OSError:
        pass
    a.close()
    s3, _, now = req('GET', '/api/config/edgeDevices')
    check(sb == 503 and reply.startswith(b'HTTP/1.1 200') and now == big, 'B gets 503 while A uploads; A still commits (%d, %r)' % (sb, reply[:20]))
    req('POST', '/api/config/edgeDevices', body=b'[]')


def case_plain():
    """Plan 09b: a save stores the bytes as sent; the device runs with them, a wrong field taking its default."""
    _, p2 = get_json('/api/config/port2')
    keep = json.dumps(p2, separators=(',', ':')).encode()
    odd = json.dumps(dict(p2, workMode=3, listenPort=4100, maxClients='five', remoteHost='h' * 80), separators=(',', ':')).encode()
    s, _, _ = req('POST', '/api/config/port2', body=odd)
    s2, _, raw = req('GET', '/api/config/port2')
    check(s == 200 and s2 == 200 and raw == odd, 'a port2 the UI would refuse (maxClients as text, an 80-char host) is stored, GET returns its bytes')
    s, _, _ = req('POST', '/api/config/port2', body=b'{"workMode":')
    s2, _, _ = req('POST', '/api/config/port2', body=b'[]')
    s3, _, raw = req('GET', '/api/config/port2')
    check(s == 400 and s2 == 400 and raw == odd, 'bad JSON and a wrong root type: 400, the stored copy kept (%d, %d)' % (s, s2))
    check(restart(), 'restart')
    log = log_since_start()
    _, st = get_json('/api/status')
    check('port2: defaults used for maxClients' in log and 'remoteHost' not in log.split('defaults used for', 1)[-1].split('\n')[0]
          and st and st['ports'][1]['state'] == 'LISTEN',
          'after the restart port2 runs as sent (listen 4100): only the wrong type, maxClients, takes its default (logged)')
    req('POST', '/api/config/port2', body=keep)


def case_factory():
    """Plan 09b: a factory reset restarts the gateway by itself into the defaults; the certificates are gone."""
    _, sysc = get_json('/api/config/system')
    req('POST', '/api/config/system', body=json.dumps(dict(sysc, hostname='before-reset'), separators=(',', ':')).encode())
    s, _, _ = req('POST', '/api/cert?scope=mqtt&kind=ca', body=real_ca(), headers={'Content-Type': 'application/octet-stream'})
    check(s == 200, 'an mqtt CA uploaded')
    s, _, _ = req('POST', '/api/factory-reset', body=b'')
    time.sleep(4)
    up = wait_up(40)
    _, sysc = get_json('/api/config/system')
    check(s == 200 and up and sysc and sysc.get('hostname') == 'sbiot-gw', 'factory reset: 200, the gateway restarts by itself into the defaults')


def edge_model(devs, per):
    out = []
    k = 0
    for d in range(devs):
        nodes = []
        for i in range(per):
            nodes.append({'name': 'p%d' % k, 'fc': 3, 'reg': i, 'dtype': 'uint16'})
            k += 1
        out.append({'name': 'dev%d' % d, 'source': 'uart', 'uart': 1, 'protocol': 'modbus', 'addr': str(d + 1), 'nodes': nodes})
    return out


def case_edge():
    model = edge_model(10, 10)  # 100 points, the STM32 limit
    text = json.dumps(model).encode()
    body = text[:-1] + b' ' * (40 * 1024 - len(text)) + b']'  # whitespace up to 40 KB: still the same model
    s, _, d = req('POST', '/api/config/edgeDevices', body=body, timeout=60)
    s2, back = get_json('/api/config/edgeDevices')
    names = [n['name'] for dev in (back or []) for n in dev.get('nodes', [])]
    check(s == 200 and s2 == 200 and len(names) == 100 and names[0] == 'p0' and names[-1] == 'p99',
          'edgeDevices: a 40 KB body (100 points) saved and read back (%d, %d points)' % (s, len(names)))
    s, _, _ = req('POST', '/api/config/edgeDevices', body=b' ' * 49153)
    check(s == 413, 'edgeDevices over 48 KB: 413 (got %d)' % s)
    req('POST', '/api/config/edgeDevices', body=b'[]')


CERT = (b'-----BEGIN CERTIFICATE-----\nMIIBMTCB2aADAgECAgkAluqkgeuV/zUwCgYIKoZIzj0EAwIwEzERMA8GA1UEAwwI\n'
        b'TW9uZ29vc2UwHhcNMjQwNTA3MTQzNzM2WhcNMzQwNTA1MTQzNzM2WjARMQ8wDQYD\n-----END CERTIFICATE-----\n')
KEY = b'-----BEGIN EC PRIVATE KEY-----\nMHcCAQEEIAVdo8UAScxG7jiuNY2UZESNX\n-----END EC PRIVATE KEY-----\n'


def real_ca():
    """A CA from mkpki.py (Plan 08b), made once into a temporary directory."""
    import subprocess
    import tempfile
    d = tempfile.mkdtemp(prefix='sbiot-pki-')
    subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'host', 'mkpki.py'), d,
                    '--now'], check=True)
    with open(os.path.join(d, 'ca.pem'), 'rb') as f:
        return f.read()


def case_misc():
    h = {'Content-Type': 'application/octet-stream'}
    s, _, d = req('POST', '/api/cert?scope=port2&kind=ca', body=CERT, headers=h)
    check(s == 400, 'a cut-off certificate: 400 (mbedTLS parses it, Plan 08b)')
    s, _, d = req('POST', '/api/cert?scope=port2&kind=ca', body=real_ca(), headers=h)
    info = json.loads(d) if s == 200 else {}
    check(s == 200 and info.get('restartNeeded') is False and 'CN=SBIOT Test CA' in info.get('subject', ''),
          'a real CA stored for port2: its subject, no restart for a port (%s)' % info)
    s, _, _ = req('POST', '/api/cert?scope=mqtt&kind=ca', body=KEY, headers=h)
    check(s == 400, 'a key as ca: 400')
    s, _, _ = req('POST', '/api/cert?scope=wifi&kind=ca', body=CERT, headers=h)
    check(s == 400, 'an unknown scope: 400')
    s, d = get_json('/api/io')
    d = d or {}
    check(s == 200 and len(d.get('do', [])) == 2 and len(d.get('di', [])) == 2 and len(d.get('ai', [])) == 2 and d.get('ao') == [],
          '/api/io: the IO state (%s)' % d)
    s, _, _ = req('POST', '/api/io', body=b'{"ao": 0, "value": 1}')
    s2, _, _ = req('POST', '/api/io', body=b'{"do": 5, "value": true}')
    check(s == 400 and s2 == 400, '/api/io: no analog outputs, no DO6: 400 (%d, %d)' % (s, s2))
    before = (get_json('/api/io')[1] or {}).get('do')
    s3, _, _ = req('POST', '/api/io', body=b'{"do": -0.5, "value": true}')
    s4, _, _ = req('POST', '/api/io', body=b'{"do": 1.9, "value": true}')
    after = (get_json('/api/io')[1] or {}).get('do')
    check(s3 == 400 and s4 == 400 and before == after, '/api/io: do -0.5 and 1.9 are no output: 400, relays unchanged (%d, %d, %s -> %s)'
          % (s3, s4, before, after))
    s, _, _ = req('GET', '/api/x')
    check(s == 404, 'an unknown endpoint: 404')


def case_clock():
    s, _, _ = req('POST', '/api/time', body=json.dumps({'time': '2026-10-15 15:30:00'}).encode())
    _, st = get_json('/api/status')
    check(s == 200 and st['system']['time'].startswith('2026-10-15 15:3'), 'POST /api/time, then system.time (%s)' % st['system']['time'])
    s, _, _ = req('POST', '/api/time', body=json.dumps({'time': '2025-02-29 00:00:00'}).encode())
    check(s == 400, 'a date that does not exist: 400')
    req('POST', '/api/config/io', body=json.dumps(get_json('/api/config/io')[1]).encode())  # a log line, dated now
    s, _, log = req('GET', '/api/log')
    check(s == 200 and b'# Since the last start' in log, '/api/log: flash and RAM parts')


def case_slow_readers(ws_port):
    before = heap_free()
    stuck = []
    for _ in range(3):
        k = socket.create_connection((HOST, PORT), timeout=5)
        k.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        k.sendall(b'GET /assets/app.js HTTP/1.1\r\nHost: x\r\n\r\n')
        stuck.append(k)
    ws, st = ws_open(ws_port, base64.b64encode(b'admin:admin').decode())
    ws.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    for i in range(30):  # log traffic for the WebSocket that does not read
        req('POST', '/api/config/io', body=json.dumps(get_json('/api/config/io')[1]).encode())
    time.sleep(2)
    s, _, body = req('GET', '/assets/app.js', auth=None, timeout=10)
    during = heap_free()
    check(s == 200 and len(body) > 40000, 'another client gets app.js while 4 clients do not read')
    check(during > before - 40000, 'heap level with clients that do not read (%d -> %d)' % (before, during))
    for k in stuck:
        k.close()
    ws.close()
    time.sleep(1)


def case_console(running_hostname):
    _, sysc = get_json('/api/config/system')
    sysc['websocketTarget'] = 3
    sysc['hostname'] = running_hostname
    req('POST', '/api/config/system', body=json.dumps(sysc).encode())
    check(restart(), 'restarted with the WebSocket console in LOG mode')
    _, dev = get_json('/api/device')
    port = dev['wsPort']
    check(dev['wsDirection'] == 3, 'wsDirection 3 after the restart')
    s, st = ws_open(port, base64.b64encode(b'admin:nope').decode())
    check(st == 401, 'WebSocket, wrong login: 401 before the handshake')
    s.close()
    tok = base64.b64encode(b'admin:admin').decode().replace('=', '%3D')
    ws, st = ws_open(port, tok)
    check(st == 101, 'WebSocket, good login (percent-encoded): 101')
    hello = ws_recv_all(ws, 1)
    ws_send(ws, 'help')
    ws_send(ws, 'status')
    req('POST', '/api/config/io', body=json.dumps(get_json('/api/config/io')[1]).encode())  # a log line
    got = hello + ws_recv_all(ws, 2)
    check(any('commands: help' in g for g in got) and any(g.startswith('firmware ') for g in got), 'help and status answered')
    check(any('io saved' in g for g in got), 'a live log line arrives')
    more = [ws_open(port, tok) for _ in range(3)]
    fifth, st5 = ws_open(port, tok)
    check(st5 == 503, 'a fifth client: refused (%d)' % st5)
    for x, _ in more:
        x.close()
    fifth.close()
    return ws, port


def case_review(ws_port):
    """The cases of Codex's 06b code review."""
    # 1. A body that comes short (the client closes its side): no effect.
    _, before = get_json('/api/config/system')
    k = socket.create_connection((HOST, PORT), timeout=5)
    body = json.dumps({'hostname': 'short-body'}).encode()
    k.sendall(b'POST /api/config/system HTTP/1.1\r\nHost: x\r\nAuthorization: ' + AUTH.encode() +
              b'\r\nContent-Length: %d\r\n\r\n' % (len(body) + 50) + body)
    k.shutdown(socket.SHUT_WR)
    time.sleep(1)
    k.close()
    _, after = get_json('/api/config/system')
    check(after['hostname'] == before['hostname'], 'a body cut short by the client: nothing saved')

    # 1b. On one connection, a POST with a body, then a GET without the login: both answered (r2).
    pb = json.dumps({'hostname': before['hostname']}).encode()
    r = raw(b'POST /api/config/system HTTP/1.1\r\nHost: x\r\nAuthorization: ' + AUTH.encode() +
            b'\r\nContent-Length: %d\r\n\r\n' % len(pb) + pb + b'GET /api/device HTTP/1.1\r\nHost: x\r\n\r\n')
    check(b'HTTP/1.1 200' in r and b'HTTP/1.1 401' in r, 'a POST, then a GET without login on one connection: 200, then 401')

    # 2. A GET, then an edgeDevices POST, pipelined in one send.
    model = edge_model(1, 2)
    eb = json.dumps(model).encode()
    r = raw(b'GET /api/device HTTP/1.1\r\nHost: x\r\nAuthorization: ' + AUTH.encode() + b'\r\n\r\n'
            b'POST /api/config/edgeDevices HTTP/1.1\r\nHost: x\r\nAuthorization: ' + AUTH.encode() +
            b'\r\nContent-Length: %d\r\n\r\n' % len(eb) + eb)
    _, back = get_json('/api/config/edgeDevices')
    names = [n['name'] for d in back for n in d['nodes']]
    check(r.count(b'HTTP/1.1 200') == 2 and names == ['p0', 'p1'], 'GET then a pipelined Edge POST: both answered, the model saved')

    # 3. Requests without reading the answers: the heap stays bounded.
    h0 = heap_free()
    k = socket.create_connection((HOST, PORT), timeout=5)
    k.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    req1 = b'GET /api/status HTTP/1.1\r\nHost: x\r\nAuthorization: ' + AUTH.encode() + b'\r\n\r\n'
    try:
        for _ in range(40):
            k.sendall(req1 * 4)
            time.sleep(0.05)
    except OSError:
        pass  # the board closed it: also bounded
    time.sleep(1)
    h1 = heap_free()
    ws, st = ws_open(ws_port, base64.b64encode(b'admin:admin').decode())
    ws.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    try:
        for _ in range(1000):
            ws_send(ws, 'status')
    except OSError:
        pass
    time.sleep(1)
    h2 = heap_free()
    k.close()
    ws.close()
    check(h1 > h0 - 20000 and h2 > h0 - 20000, 'requests and console commands without reading: heap bounded (%d, %d, %d)' % (h0, h1, h2))

    # 4. Console sockets that never finish their handshake: limited, and closed after 5 s. Only an EOF
    # or a reset counts as closed (a timeout means the socket is still open); the new client tries
    # while the test still holds its sockets, so the board must have taken them back itself (r2).
    def closed_by_board(x, wait):
        x.settimeout(wait)
        try:
            return x.recv(1) == b''
        except ConnectionResetError:
            return True
        except socket.timeout:
            return False
    pend = [socket.create_connection((HOST, ws_port), timeout=5) for _ in range(8)]
    time.sleep(0.5)
    refused = sum(closed_by_board(x, 0.2) for x in pend)
    time.sleep(6)
    late = sum(closed_by_board(x, 0.5) for x in pend)
    ws, st = ws_open(ws_port, base64.b64encode(b'admin:admin').decode())
    check(refused >= 3 and late == 8 and st == 101,
          'pending console sockets: %d refused at once, %d of 8 closed by the board after 5 s, a client then gets in (%d)' % (refused, late, st))
    ws.close()
    for x in pend:
        x.close()

    # 5. Edge GETs that hold 4 generations: the fifth gets 503, not an empty 200; closing frees them.
    big = json.dumps(edge_model(10, 10)).encode()
    big = big[:-1] + b" " * (17 * 1024 - len(big)) + b"]"  # stored as sent (Plan 09b): 3 records, so a slow GET stays pinned
    holders = []
    for i in range(4):
        req('POST', '/api/config/edgeDevices', body=big.replace(b'"p0"', b'"q%d"' % i), timeout=60)
        k = socket.create_connection((HOST, PORT), timeout=5)
        k.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)
        k.sendall(b'GET /api/config/edgeDevices HTTP/1.1\r\nHost: x\r\nAuthorization: ' + AUTH.encode() + b'\r\n\r\n')
        holders.append(k)
        time.sleep(0.5)
    req('POST', '/api/config/edgeDevices', body=big, timeout=60)
    s5, _, _ = req('GET', '/api/config/edgeDevices')
    for k in holders:
        k.close()
    time.sleep(1)
    s6, back = get_json('/api/config/edgeDevices')
    check(s5 == 503 and s6 == 200 and len(back) == 10, 'a fifth generation pinned: 503; after the readers close: 200 (%d, %d)' % (s5, s6))
    req('POST', '/api/config/edgeDevices', body=b'[]')


def case_ota(build):
    img = open(os.path.join(build, 'app.img'), 'rb').read()
    s, _, d = req('POST', '/api/ota', body=img, headers={'Content-Type': 'application/octet-stream'}, timeout=120)
    check(s == 200, 'OTA over the web: accepted (%d %s)' % (s, d[:80]))
    time.sleep(5)
    up = wait_up(60)
    _, dev = get_json('/api/device') if up else (0, None)
    check(up and dev and dev['firmware'], 'and the gateway is back, firmware %s' % (dev['firmware'] if dev else '-'))


def main():
    build = sys.argv[1]
    if not wait_up(5):
        print('the board does not answer on %s' % HOST)
        return 1
    if len(sys.argv) > 2:  # only the named cases: web_check.py BUILD plain factory
        for name in sys.argv[2:]:
            globals()['case_' + name]()
        print('bench: %s' % ('ok' if not failures else 'FAILED (%d)' % len(failures)))
        return 1 if failures else 0
    case_auth()
    case_limits()
    running = case_configs()
    case_edge()
    case_plain()
    case_misc()
    case_clock()
    ws, ws_port = case_console(running)
    ws.close()
    case_slow_readers(ws_port)
    case_review(ws_port)
    case_ota(build)
    # Back to the console off, as the factory has it.
    _, sysc = get_json('/api/config/system')
    sysc['websocketTarget'] = 0
    req('POST', '/api/config/system', body=json.dumps(sysc).encode())
    restart()
    print('bench: %s' % ('ok' if not failures else 'FAILED (%d)' % len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
