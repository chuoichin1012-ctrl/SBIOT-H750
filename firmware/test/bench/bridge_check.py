#!/usr/bin/env python3
"""Plan 08a bench check: the per-port network bridge (spec §6.4, §5.6), the ESP32's test_bridge.py without
RFC 2217, PUSR and TLS. UART1's bridge is under test; UART2, wired to it (the RS485 loop of 07a), is the
device side through the bench console (E/F). Peers the board dials run on Windows (win_peer.py, WSL is
behind NAT); peers that dial the board run here.

    python3 stm32/firmware/test/bench/bridge_check.py [case ...]

Cases: tcpserver modbus tcpclient mbclient udp http offline idle load twoports. Prints one line per check
and "bench: ok"; exits 1 on any failure. Needs the firmware's Python allowed through the Windows firewall.
"""
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402
import serial_check as sc  # noqa: E402  (console, frames, counters)
import web_check as w  # noqa: E402

import bench_env  # noqa: E402  (Plan 11b: the bench's addresses)
GW, WIN, WIN_PY = bench_env.GW, bench_env.WIN, bench_env.WIN_PY  # bench_env.set_board updates GW after a move
failures = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


def rtu(body):
    return body + struct.pack('<H', crc16(body))


def mbap(tid, unit_pdu):
    return struct.pack('>HHH', tid, 0, len(unit_pdu)) + unit_pdu


def recv_some(s, timeout=3, want=1):
    s.settimeout(0.2)
    data, end = b'', time.time() + timeout
    while time.time() < end and len(data) < want:
        try:
            d = s.recv(4096)
            if not d:
                break
            data += d
        except socket.timeout:
            pass
    return data


def setup(port=1, sys_extra=None, **fields):
    """port<n> fields (on the stored config), system fields, then a restart."""
    _, p = w.get_json('/api/config/port%d' % port)
    p.update(fields)
    s1, _, d1 = w.req('POST', '/api/config/port%d' % port, body=json.dumps(p).encode())
    _, sysc = w.get_json('/api/config/system')
    sysc.update({'bufferOffline': 0, 'idleRestart': 0})
    sysc.update(sys_extra or {})
    s2, _, d2 = w.req('POST', '/api/config/system', body=json.dumps(sysc).encode())
    ok = s1 == 200 and s2 == 200 and w.restart()
    time.sleep(2)
    return ok


def base(**kw):
    f = {'workMode': 3, 'socketMode': 0, 'listenPort': 23, 'maxClients': 8, 'clientLimitAction': 0, 'allowedClients': '',
         'modbusPoll': False, 'modbusException': False, 'netHeartbeat': False, 'registration': 0, 'tls': 0,
         'remoteHost': WIN, 'remotePort': 9000, 'localPort': 0, 'reconnectInterval': 2, 'packetInterval': 0, 'packetLength': 0}
    f.update(kw)
    return f


def last_hex(port=2):
    """The frames UART2 (the device side) got since the last call, and the hex of the last one."""
    lines = sc.console([(0.3, 'F%d' % port)], 1.5)
    n = sc.find(lines, r'F%d frames (\d+) bytes (\d+)' % port)
    h = sc.find(lines, r'F%d last (\S+)' % port)
    return (int(n.group(1)) if n else -1), (h.group(1) if h and h.group(1) != '-' else '')


def send_serial(data, port=2):
    """Bytes from the device side (UART2) to UART1's bridge."""
    sc.console([(0.3, 'E%d 4 %s' % (port, data.hex()))], 1.0)


def peer(mode, **args):
    """A peer on Windows: started now, its events read when it ends (wait_peer)."""
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'win_peer.py')
    dst = '/mnt/c/Users/Public/sbiot/win_peer.py'
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(src, 'rb') as a, open(dst, 'wb') as b:
        b.write(a.read())
    argv = [WIN_PY, r'C:\Users\Public\sbiot\win_peer.py', mode] + ['%s=%s' % kv for kv in args.items()]
    p = subprocess.Popen(argv, cwd='/mnt/c', stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(1.0)  # Python's start on Windows
    return p


def wait_peer(p, extra=5):
    try:
        out, _ = p.communicate(timeout=extra + 60)
    except subprocess.TimeoutExpired:
        p.kill()
        out, _ = p.communicate()
    ev = []
    for line in out.splitlines():
        try:
            ev.append(json.loads(line))
        except ValueError:
            pass
    return ev


def data_of(ev):
    return b''.join(bytes.fromhex(e['hex']) for e in ev if e.get('ev') == 'data')


def port_state(n=1):
    _, st = w.get_json('/api/status')
    return st['ports'][n - 1] if st else {}


# ---- cases ------------------------------------------------------------------------------------

def case_tcpserver():
    check(setup(1, **base()), 'set up UART1: TCP server on 23')
    last_hex()
    a, b = socket.create_connection((GW, 23), 5), socket.create_connection((GW, 23), 5)
    time.sleep(0.5)
    a.sendall(b'hello-from-net')
    time.sleep(0.5)
    n, h = last_hex()
    check(bytes.fromhex(h) == b'hello-from-net', 'TCP server: network -> serial (%s)' % h)
    send_serial(b'serial-to-all')
    ra, rb = recv_some(a, 2, 13), recv_some(b, 2, 13)
    check(ra == rb == b'serial-to-all', 'TCP server: serial -> every client (%r, %r)' % (ra, rb))
    st = port_state()
    check(st.get('state') == 'CONNECTED(2)' and st.get('tx', 0) >= 14 and st.get('rx', 0) >= 13, 'status: CONNECTED(2) with byte counters (%s)' % st)
    a.close()
    b.close()
    _, p = w.get_json('/api/config/port1')
    was = p.get('allowedClients', '')
    p['allowedClients'] = '300.1.1.1'
    s, _, _ = w.req('POST', '/api/config/port1', body=json.dumps(p).encode())
    # Plan 09b: the UI refuses it (allowListError); the gateway stores what it is sent, and its bridge ignores a
    # list it cannot read (anyone, logged).
    check(s == 200, 'allowedClients: a malformed list is stored as sent; the UI is the one to refuse it (%d)' % s)
    p['allowedClients'] = was
    w.req('POST', '/api/config/port1', body=json.dumps(p).encode())
    check(setup(1, **base(allowedClients='192.168.0.99')), 'set up allowedClients 192.168.0.99')
    c = socket.create_connection((GW, 23), 5)
    closed = recv_some(c, 2) == b''
    c.close()
    check(closed, 'allowedClients: a client outside the list is refused')
    # maxClients 2: drop the oldest, then refuse the third
    check(setup(1, **base(maxClients=2, clientLimitAction=0)), 'set up maxClients 2, drop the oldest')
    cl = [socket.create_connection((GW, 23), 5) for _ in range(2)]
    time.sleep(0.5)
    cl.append(socket.create_connection((GW, 23), 5))
    time.sleep(0.5)
    send_serial(b'who-gets-it')
    got = [recv_some(s, 1.5, 11) for s in cl]
    check(got[0] == b'' and got[1] == got[2] == b'who-gets-it', 'clientLimitAction 0: the oldest dropped (%s)' % got)
    for s in cl:
        s.close()
    check(setup(1, **base(maxClients=2, clientLimitAction=1)), 'set up maxClients 2, refuse the new one')
    cl = [socket.create_connection((GW, 23), 5) for _ in range(2)]
    time.sleep(0.5)
    cl.append(socket.create_connection((GW, 23), 5))
    time.sleep(0.5)
    send_serial(b'who-gets-it')
    got = [recv_some(s, 1.5, 11) for s in cl]
    check(got[0] == got[1] == b'who-gets-it' and got[2] == b'', 'clientLimitAction 1: the third refused (%s)' % got)
    for s in cl:
        s.close()
    # the §5.6 accept gate, with the reserve raised from the console
    check(setup(1, **base()), 'set up the TCP server again')
    sc.console([(0.3, 'Y64')], 1)
    last_hex()
    g = socket.create_connection((GW, 23), 5)
    g.sendall(b'gated')
    time.sleep(2)
    n0, _ = last_hex()
    sc.console([(0.3, 'Y4')], 1)
    time.sleep(1)
    n1, h1 = last_hex()
    g.close()
    check(n0 == 0 and bytes.fromhex(h1) == b'gated', 'the netconn gate: no accept while below the reserve, then accepted (%d, %s)' % (n0, h1))
    # A client that never reads holds its port's other clients until it is dropped, after about 10 s
    # without a byte taken (spec §5.6 exception); then the other receives at the full rate again. Both
    # clients run on Windows: through WSL's NAT a zero-window connection stalled the other one too.
    p = peer('stuckpair', host=GW, port=23, secs=36)
    lines = board.listen(34, sends=[(0.3, 'E'), (0.4, '2 3 32\r')])
    ev = wait_peer(p)
    t_conn = next((e['t'] for e in ev if e.get('ev') == 'connected'), 0)
    got = [(e['t'] / 1000.0, e['n']) for e in ev if e.get('ev') == 'got']
    gaps = [(a[0], b[0]) for a, b in zip(got, got[1:]) if b[0] - a[0] > 2.0]
    dropped = any('took nothing for 10 s' in l for _, l in lines)
    m = sc.find([l for _, l in lines], r'E2 streamed (\d+) bytes')
    rate = int(m.group(1)) / 32.0 if m else 0  # bytes a second from the device side
    after = [(t, n) for t, n in got if gaps and t >= gaps[0][1]]
    dur = after[-1][0] - after[0][0] if len(after) > 1 else 0
    got_after = after[-1][1] - after[0][1] if len(after) > 1 else 0
    # Codex code review r2: after the recovery the reader must go on at the stream's rate, to the end.
    check(m is not None and rate > 0 and dropped and len(gaps) == 1 and 9 <= gaps[0][1] - gaps[0][0] <= 13 and dur >= 15
          and got_after >= 0.9 * rate * dur,  # the stream's own count is required (Codex code review r3)
          'a client that never reads: dropped (%s); the other waited %s s once, then got %d of %d bytes over %.1f s'
          % (dropped, round(gaps[0][1] - gaps[0][0], 1) if gaps else '-', got_after, rate * dur, dur))


def case_modbus():
    check(setup(1, **base(socketMode=1, modbusException=True, modbusPoll=True, modbusPollTimeout=300)),
          'set up UART1: TCP server, Modbus TCP gateway, exception')
    s = socket.create_connection((GW, 23), 5)
    answer = rtu(bytes([9, 3, 4, 0x12, 0x34, 0x56, 0x78]))
    sc.console([(0.3, 'E2 2 0 ' + answer.hex())], 1)
    s.sendall(mbap(7, bytes([9, 3, 0, 0, 0, 2])))
    r = recv_some(s, 2, 13)
    check(r == mbap(7, bytes([9, 3, 4, 0x12, 0x34, 0x56, 0x78])), 'Modbus TCP -> RTU slave -> Modbus TCP reply (%s)' % r.hex())
    req = mbap(8, bytes([9, 3, 0, 0, 0, 2]))
    s.sendall(req[:5])
    time.sleep(0.3)
    s.sendall(req[5:])
    r = recv_some(s, 2, 13)
    check(r == mbap(8, bytes([9, 3, 4, 0x12, 0x34, 0x56, 0x78])), 'a request split over two segments (%s)' % r.hex())
    s.sendall(mbap(10, bytes([9, 3, 0, 0, 0, 2])) + mbap(11, bytes([9, 3, 0, 0, 0, 2])))
    r = recv_some(s, 3, 26)
    check(r == mbap(10, bytes([9, 3, 4, 0x12, 0x34, 0x56, 0x78])) + mbap(11, bytes([9, 3, 4, 0x12, 0x34, 0x56, 0x78])),
          'two requests in one segment: both answered (%s)' % r.hex())
    sc.console([(0.3, 'E2 0')], 1)
    s.sendall(mbap(12, bytes([9, 3, 0, 0, 0, 1])))
    r = recv_some(s, 2, 9)
    check(r == mbap(12, bytes([9, 0x83, 0x0B])), 'silent slave -> exception 0x0B (%s)' % r.hex())
    _, io = w.get_json('/api/config/io')
    unit = io.get('modbusAddress', 100)
    s.sendall(mbap(13, bytes([unit, 5, 0, 0, 0xFF, 0])))
    r = recv_some(s, 2, 12)
    check(r == mbap(13, bytes([unit, 5, 0, 0, 0xFF, 0])), 'IO slave (io.modbusAddress %d): write coil = DO1 on (%s)' % (unit, r.hex()))
    check(w.get_json('/api/io')[1]['do'][0] is True, 'DO1 really on')
    s.sendall(mbap(14, bytes([unit, 1, 0, 0, 0, 1])))
    r = recv_some(s, 2, 10)
    check(r == mbap(14, bytes([unit, 1, 1, 1])), 'IO slave: read coils (%s)' % r.hex())
    s.sendall(mbap(15, bytes([unit, 5, 0, 0, 0, 0])))
    recv_some(s, 2, 12)
    s.close()
    # polling: the answer goes only to the asking client
    check(setup(1, **base(socketMode=0, modbusPoll=True, modbusPollTimeout=300)), 'set up polling')
    a, b = socket.create_connection((GW, 23), 5), socket.create_connection((GW, 23), 5)
    reply = rtu(bytes([1, 3, 2, 0, 42]))
    sc.console([(0.3, 'E2 2 0 ' + reply.hex())], 1)
    a.sendall(rtu(bytes([1, 3, 0, 0, 0, 1])))
    ra, rb = recv_some(a, 2, 7), recv_some(b, 1, 1)
    sc.console([(0.3, 'E2 0')], 1)
    a.close()
    b.close()
    check(ra == reply and rb == b'', 'polling: the answer goes only to the asking client (%s, %s)' % (ra.hex(), rb.hex()))


def case_tcpclient():
    p = peer('tcpserver', port=9000, secs=25, send_hex=b'xyz'.hex(), send_ms=3000, drop_ms=8000)
    check(setup(1, **base(workMode=1, registration=1, registrationSend=0, registrationHex=0, registrationData='ID42',
                          netHeartbeat=True, netHeartbeatContent=0, netHeartbeatHex=0, netHeartbeatData='HB', netHeartbeatInterval=2)),
          'set up UART1: TCP client to the PC, registration, heartbeat')
    last_hex()
    time.sleep(1)
    send_serial(b'abc')
    time.sleep(4)
    n, h = last_hex()
    ev = wait_peer(p)
    data = data_of(ev)
    accepts = [e for e in ev if e.get('ev') == 'accept']
    check(data.startswith(b'ID42'), 'TCP client: the registration packet on connect (%r)' % data[:12])
    check(b'abc' in data, 'TCP client: serial -> server')
    check(bytes.fromhex(h).endswith(b'xyz') if h else False, 'TCP client: server -> serial (%s)' % h)
    check(b'HB' in data, 'TCP client: network heartbeat in a silence')
    check(len(accepts) >= 2, 'TCP client: reconnects after the server dropped it (%d connections)' % len(accepts))
    # short connection
    check(setup(1, **base(workMode=1, socketMode=2, shortConnectionTime=3)), 'set up a short connection of 3 s')
    p = peer('tcpserver', port=9000, secs=15)  # after the restart: the previous mode would dial it
    time.sleep(3)
    send_serial(b'burst')
    ev = wait_peer(p)
    acc = [e for e in ev if e.get('ev') == 'accept']
    closes = [e for e in ev if e.get('ev') == 'close']
    data = [e for e in ev if e.get('ev') == 'data']
    check(len(acc) == 1 and acc[0]['t'] > 3000, 'short connection: no connection without data, a dial on serial data')
    check(data and bytes.fromhex(data[0]['hex']) == b'burst', 'short connection: the data arrives')
    check(closes and 2000 <= closes[0]['t'] - data[0]['t'] <= 6000, 'short connection: closed after the idle time (%s ms)'
          % (closes[0]['t'] - data[0]['t'] if closes and data else '-'))


def case_mbclient():
    check(setup(1, **base(workMode=1, socketMode=1, modbusException=True, modbusPollTimeout=500)), 'set up UART1: Modbus TCP client')
    p = peer('tcpserver', port=9000, secs=10, modbus=1)
    last_hex()
    time.sleep(2)
    send_serial(rtu(bytes([7, 3, 0, 4, 0, 1])))
    time.sleep(1)
    n, h = last_hex()
    ev = wait_peer(p)
    adu = data_of(ev)
    check(len(adu) == 12 and adu[2:] == bytes([0, 0, 0, 6, 7, 3, 0, 4, 0, 1]), 'an RTU request from the serial master arrives as Modbus TCP (%s)' % adu.hex())
    check(h == rtu(bytes([7, 3, 2, 0x12, 0x34])).hex(), "the slave's reply reaches the serial master as RTU (%s)" % h)
    check(setup(1, **base(workMode=1, socketMode=1, modbusException=True, modbusPollTimeout=500)), 'set up again, the slave silent')
    p = peer('tcpserver', port=9000, secs=10, modbus=1, silent=1)  # after the restart
    last_hex()
    time.sleep(2)
    send_serial(rtu(bytes([7, 3, 0, 4, 0, 1])))
    time.sleep(1.5)
    n, h = last_hex()
    wait_peer(p)
    check(h == rtu(bytes([7, 0x83, 0x0B])).hex(), 'a silent Ethernet slave -> exception 0x0B to the serial master (%s)' % h)


def case_udp():
    check(setup(1, **base(workMode=0, remotePort=9001, localPort=9001)), 'set up UART1: UDP client')
    p = peer('udp', port=9001, secs=8, send_hex=b'udp-down'.hex(), send_ms=4000, to='%s:9001' % GW)
    last_hex()
    time.sleep(1)
    send_serial(b'udp-up')
    ev = wait_peer(p)
    n, h = last_hex()
    check(data_of(ev) == b'udp-up', 'UDP client: serial -> datagram (%r)' % data_of(ev))
    check(h and bytes.fromhex(h) == b'udp-down', 'UDP client: datagram -> serial (%s)' % h)
    check(setup(1, **base(workMode=2, localPort=9002)), 'set up UART1: UDP server')
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.bind(('0.0.0.0', 0))
    u.sendto(b'who', (GW, 9002))
    time.sleep(0.5)
    n, h = last_hex()
    check(h and bytes.fromhex(h) == b'who', 'UDP server: datagram -> serial (%s)' % h)
    send_serial(b'me')
    u.settimeout(2)
    try:
        d, _ = u.recvfrom(100)
    except socket.timeout:
        d = b''
    check(d == b'me', 'UDP server: the reply goes to the last sender (%r)' % d)
    check(setup(1, **base(workMode=2, localPort=9002, allowedClients='192.168.0.99')), 'set up allowedClients on the UDP server')
    u.sendto(b'not-me', (GW, 9002))
    time.sleep(0.5)
    n, h = last_hex()
    check(n == 0, 'UDP server: a sender outside allowedClients is ignored (%d frames)' % n)
    u.close()
    check(setup(1, **base(workMode=0, socketMode=4, remoteHost='239.1.2.3', remotePort=9003, localPort=0)), 'set up multicast 239.1.2.3:9003')
    last_hex()
    p = peer('mcast', group='239.1.2.3', port=9003, iface=WIN, send_hex=b'group'.hex(), send_ms=1000)
    wait_peer(p)
    time.sleep(0.5)
    n, h = last_hex()
    check(h and bytes.fromhex(h) == b'group', 'multicast: joined, a group datagram reaches the port (%s)' % h)


def case_http():
    check(setup(1, **base(workMode=4, httpHost=WIN, httpPort=9004, httpMethod=0, httpPath='/data?v=', httpHeaders='k1: a',
                          httpStripHeaders=True, httpTimeout=5)), 'set up UART1: HTTP client, GET')
    p = peer('http', port=9004, secs=8, reply='OK-GET')
    last_hex()
    send_serial(b'7')
    time.sleep(3)
    ev = wait_peer(p)
    n, h = last_hex()
    req = [e for e in ev if e.get('ev') == 'request']
    check(req and req[0]['method'] == 'GET' and req[0]['path'] == '/data?v=7' and req[0]['headers'].get('k1') == 'a',
          'HTTP GET: the data in the URL, the extra header sent (%s)' % (req[0] if req else '-'))
    check(h and bytes.fromhex(h) == b'OK-GET', 'HTTP GET: the body only to serial (%s)' % h)
    check(setup(1, **base(workMode=4, httpHost=WIN, httpPort=9004, httpMethod=1, httpPath='/in', httpHeaders='',
                          httpStripHeaders=False, httpTimeout=5)), 'set up HTTP POST, headers kept')
    p = peer('http', port=9004, secs=8, reply='OK-POST')
    last_hex()
    send_serial(b'payload-1')
    time.sleep(3)
    ev = wait_peer(p)
    n, h = last_hex()
    req = [e for e in ev if e.get('ev') == 'request']
    got = bytes.fromhex(h) if h else b''
    check(req and req[0]['method'] == 'POST' and req[0]['path'] == '/in' and bytes.fromhex(req[0]['body']) == b'payload-1',
          'HTTP POST: the data as the body')
    check(got.startswith(b'HTTP/1.0 200'), 'HTTP POST: the headers kept when not stripped (%r)' % got[:20])


def case_offline():
    for keep in (1, 0):
        check(setup(1, sys_extra={'bufferOffline': keep}, **base(workMode=1)), 'set up a TCP client, bufferOffline %d, server down' % keep)
        time.sleep(1)
        for k in range(3):
            send_serial(b'frame%d' % k)
        p = peer('tcpserver', port=9000, secs=10)
        ev = wait_peer(p)
        data = data_of(ev)
        if keep:
            check(data == b'frame0frame1frame2', 'bufferOffline: the 3 frames arrive in order once the server is up (%r)' % data)
        else:
            check(b'frame' not in data, 'without bufferOffline: they are dropped (%r)' % data)


def wait_restart(limit_s):
    """The uptime the board reached before it restarted (its last boot's life, s), or None within limit_s."""
    t0, last = time.time(), None
    while time.time() - t0 < limit_s:
        st = w.get_json('/api/status')[1] if w.wait_up(1) else None
        up = st['system']['uptime'] if st else None
        if up is not None and last is not None and up < last:
            return last
        if up is not None:
            last = up
        time.sleep(1)
    return None


def case_idle():
    # idleRestart takes 0 or 60..65535 s (the config refuses less). Heartbeats are no traffic.
    check(setup(1, sys_extra={'idleRestart': 60}, **base(workMode=1, netHeartbeat=True, netHeartbeatContent=0, netHeartbeatHex=0,
                                                         netHeartbeatData='HB', netHeartbeatInterval=5)),
          'set up idleRestart 60 s, a TCP client sending only heartbeats')
    t_set = time.time()
    p = peer('tcpserver', port=9000, secs=85)
    seen = wait_restart(85)
    ev = wait_peer(p)
    hbs = data_of(ev).count(b'HB')
    check(seen is not None and 58 <= seen <= 64 and hbs >= 8,
          'idleRestart 60: restarted after %s s with only heartbeats flowing (%d of them)' % (round(seen) if seen else '-', hbs))
    check(setup(1, sys_extra={'idleRestart': 60}, **base(workMode=3)), 'set up idleRestart 60 s, traffic every 10 s')
    s = socket.create_connection((GW, 23), 5)
    restarted = False
    t0 = time.time()
    while time.time() - t0 < 90:
        try:
            s.sendall(b'tick')
        except OSError:
            restarted = True
            break
        time.sleep(10)
        st = w.get_json('/api/status')[1]
        if st and st['system']['uptime'] < 10:
            restarted = True
    s.close()
    check(not restarted, 'with traffic every 10 s: no restart in 90 s')
    setup(1, **base())


def case_load():
    _, io = w.get_json('/api/config/io')
    io.update({'do0Mode': 1, 'do0FlipDelay': 2})
    w.req('POST', '/api/config/io', body=json.dumps(io).encode())
    _, p2 = w.get_json('/api/config/port2')
    p2['baudRate'] = 230400
    w.req('POST', '/api/config/port2', body=json.dumps(p2).encode())
    check(setup(1, **base(baudRate=230400)), 'set up a TCP server, UART1 and UART2 at 230 400, DO1 pulse 2 s')
    g0 = bench_env.gates('bridge load+flood before')
    s = socket.create_connection((GW, 23), 5)
    a0 = adc_pace()  # on the board's clock
    import threading
    stop = [False]
    got = [0]

    def drain():  # the PC only reads: the RS485 loop is half duplex, so nothing may go the other way
        s.settimeout(0.2)
        while not stop[0]:
            try:
                d = s.recv(65536)
                if not d:
                    break
                got[0] += len(d)
            except socket.timeout:
                pass
    th = threading.Thread(target=drain, daemon=True)
    th.start()
    flood = subprocess.Popen([WIN_PY, '-c', (
        'import socket,time\n'
        's=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)\n'
        'time.sleep(5);t=time.time();n=0\n'
        'while time.time()-t<40:\n'
        '    s.sendto(b"x"*1000,("%s",9));n+=1\n'
        'print(n)\n' % GW)], cwd='/mnt/c', stdout=subprocess.PIPE, text=True)
    t0 = time.time()

    def pulse():  # 30 s in, with the trace on: DO1's 2 s pulse, timed by the board
        time.sleep(30)
        w.req('POST', '/api/io', body=json.dumps({'do': 0, 'value': True}).encode())
    lines = [l for _, l in board.listen(64, sends=[(0.3, 'E'), (0.4, '2 3 60\r'), (29.0, 'D'), (35.0, 'D')], during=pulse)]
    out, _ = flood.communicate()
    a1 = adc_pace()
    stop[0] = True
    th.join(2)
    s.close()
    wins = (a1[1] - a0[1]) / (a1[0] - a0[0]) if a0 and a1 else -1
    late = a1[2] - a0[2] if a0 and a1 else -1
    outs = [(int(m.group(2)), int(m.group(1), 16)) for l in lines for m in [re.search(r'outputs 0x(\w+) at (\d+) ms', l)] if m]
    on = [t for t, v in outs if v & 1]
    off = [t for t, v in outs if not v & 1 and on and t > on[0]]
    pulse_ms = off[0] - on[0] if on and off else -1
    m = sc.find(lines, r'E2 streamed (\d+) bytes')
    streamed = int(m.group(1)) if m else 0
    packets = int(out.strip() or 0)
    if pulse_ms < 0:
        print('     trace lines:', [l for l in lines if 'output' in l or 'D ' in l][:8])
    check(streamed > 1000000 and got[0] >= 0.95 * streamed and packets > 10000 and 9.5 <= wins <= 10.5 and late == 0 and 1980 <= pulse_ms <= 2020,
          'under load (%d B streamed into the bridge, %d B at the PC, a LAN flood of %d packets): ADC %.2f windows/s, late +%d; '
          'DO pulse %d ms' % (streamed, got[0], packets, wins, late, pulse_ms))
    ok_g, _, why_g = bench_env.qualify('bridge load+flood', g0)
    check(ok_g, 'gates after the bridge load and the flood: %s' % why_g)
    io.update({'do0Mode': 0})
    w.req('POST', '/api/config/io', body=json.dumps(io).encode())
    p2['baudRate'] = 115200
    w.req('POST', '/api/config/port2', body=json.dumps(p2).encode())
    setup(1, **base(baudRate=115200))


def case_twoports():
    _, p2 = w.get_json('/api/config/port2')
    p2.update({'workMode': 0, 'socketMode': 0, 'remoteHost': WIN, 'remotePort': 9005, 'localPort': 9005, 'udpAnyRemotePort': 1,
               'registration': 0, 'tls': 0})  # the PC's datagrams come from any port
    w.req('POST', '/api/config/port2', body=json.dumps(p2).encode())
    up = [b'up%03d' % i for i in range(30)]
    down = [b'dn%03d' % i for i in range(30)]
    p = peer('udp', port=9005, secs=45, send_hex=b''.hex(), to='%s:9005' % GW)
    check(setup(1, **base()), 'set up UART1 TCP server, UART2 UDP client to the PC')
    s = socket.create_connection((GW, 23), 5)
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # the PC's datagrams to UART2 go from Windows
    got_tcp = b''
    for i in range(30):
        s.sendall(up[i])
        time.sleep(0.5)
        subprocess.run([WIN_PY, '-c', 'import socket;socket.socket(2,2).sendto(%r,("%s",9005))' % (down[i], GW)], cwd='/mnt/c')
        got_tcp += recv_some(s, 0.3, 5)
    got_tcp += recv_some(s, 2, 1000)
    s.close()
    ev = wait_peer(p, 1)
    udp_in = data_of(ev)
    check(udp_in == b''.join(up), 'UART1 TCP -> wire -> UART2 UDP: 30 frames whole, in order (%d bytes)' % len(udp_in))
    check(got_tcp == b''.join(down), 'UART2 UDP -> wire -> UART1 TCP: 30 frames whole, in order (%d bytes)' % len(got_tcp))
    p2.update({'workMode': 3, 'listenPort': 24})
    w.req('POST', '/api/config/port2', body=json.dumps(p2).encode())
    setup(1, **base())


def adc_pace():
    """(board seconds, ADC windows, late): the board's own clock, not the PC's after a console round trip."""
    lines = sc.console([(0.3, 'a'), (0.5, 'i')], 1.2)
    m = sc.find(lines, r'windows (\d+) late (\d+)')
    u = sc.find(lines, r'uptime_ms=(\d+)')
    return (int(u.group(1)) / 1000.0, int(m.group(1)), int(m.group(2))) if m and u else None


def case_emptyudp():
    """A flood of empty datagrams must not starve the io task (Codex code review r1)."""
    check(setup(1, **base(workMode=2, localPort=9002)), 'set up UART1: UDP server on 9002')
    a0 = adc_pace()
    flood = subprocess.run([WIN_PY, '-c', (
        'import socket,time\n'
        's=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)\n'
        't=time.time();n=0\n'
        'while time.time()-t<20:\n'
        '    s.sendto(b"",("%s",9002));n+=1\n'
        'print(n)\n' % GW)], cwd='/mnt/c', capture_output=True, text=True)
    a1 = adc_pace()
    n = int(flood.stdout.strip() or 0)
    wins = (a1[1] - a0[1]) / (a1[0] - a0[0]) if a0 and a1 else -1
    late = a1[2] - a0[2] if a0 and a1 else -1
    check(n > 10000 and 9.5 <= wins <= 10.5 and late == 0,
          'a flood of %d empty datagrams: ADC %.2f windows/s, late +%d (the io task keeps its pace)' % (n, wins, late))


def case_fair():
    """More than 8 busy clients: every one is served (Codex code review r1)."""
    check(setup(1, **base(socketMode=1, maxClients=12)), 'set up UART1: TCP server, Modbus gateway, 12 clients')
    _, io = w.get_json('/api/config/io')
    unit = io.get('modbusAddress', 100)
    import threading
    cls = [socket.create_connection((GW, 23), 5) for _ in range(10)]
    counts = [0] * 10
    stop = [False]

    def run(i):
        s = cls[i]
        s.settimeout(0.5)
        req = mbap(1, bytes([unit, 1, 0, 0, 0, 1]))
        s.sendall(req * 4)  # keep requests queued: the client always has data to read
        while not stop[0]:
            try:
                d = s.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not d:
                break
            k = len(d) // 10
            counts[i] += k
            try:
                s.sendall(req * k)
            except OSError:
                break
    th = [threading.Thread(target=run, args=(i,), daemon=True) for i in range(10)]
    for t in th:
        t.start()
    time.sleep(10)
    stop[0] = True
    for t in th:
        t.join(2)
    for s in cls:
        s.close()
    check(min(counts) > 0 and min(counts) >= 0.2 * (sum(counts) / 10.0),
          '10 busy clients on the Modbus gateway: answers per client %s' % counts)


CASES = {'tcpserver': case_tcpserver, 'modbus': case_modbus, 'tcpclient': case_tcpclient, 'mbclient': case_mbclient,
         'udp': case_udp, 'http': case_http, 'offline': case_offline, 'idle': case_idle, 'load': case_load,
         'twoports': case_twoports, 'emptyudp': case_emptyudp, 'fair': case_fair}


def main():
    if not w.wait_up(5):
        print('the board does not answer on %s' % GW)
        return 1
    for name in sys.argv[1:] or CASES:
        CASES[name]()
    print('bench: %s' % ('ok' if not failures else 'FAILED (%d)' % len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
