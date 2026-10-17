#!/usr/bin/env python3
"""Plan 08c bench check: the Edge runtime, the ESP32's test_edge.py; its MQTT part (channel 0) since Plan 09.

UART1 is the Edge bus. Its devices are played by this PC through the RS485 loop: port2 is a TCP server,
and BusSlave connects to it and answers what UART1's requests put on UART2 (Modbus RTU unit 5, a dead
unit 6, a DL/T645 meter). The Modbus TCP device runs on Windows (win_peer.py tcpserver modbus=1). Reports
and remote access use port1's socket (channel 1).

    python3 stm32/firmware/test/bench/edge_check.py [case ...]

Cases: basic change schedule remote split gwrtu contention dead late shortreport partial bigreport memory di mqtt (di needs
DO1 wired to DI1; mqtt runs mosquitto in WSL, as mqtt_check.py).
Prints one line per check and "bench: ok".
"""
import datetime
import json
import os
import re
import socket
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_check as bc  # noqa: E402  (base, peer, wait_peer, check, crc16, rtu, mbap)
import web_check as w  # noqa: E402

GW, WIN = bc.GW, bc.WIN
MBTCP = 1502
check = bc.check
METER = '000000000001'


# ---- the bus devices, through port2 ---------------------------------------------------------------------

class BusSlave(threading.Thread):
    """Modbus RTU unit 5 (registers and coils), a dead unit 6, and a DL/T645 meter, on port2's socket."""

    def __init__(self):
        super().__init__(daemon=True)
        self.regs = {10: 250, 11: 7, 12: 0x4120, 13: 0}
        self.coils = {3: 1}
        self.meter = {0x02010100: bytes([0x01, 0x22])}  # 220.1 V: BCD 2201, little-endian
        self.log = []  # (t, unit, fc, start, count)
        self.raw = b''
        self.stop = False
        self.sock = None
        for _ in range(40):
            try:
                self.sock = socket.create_connection((GW, 24), 3)
                break
            except OSError:
                time.sleep(0.5)
        self.sock.settimeout(0.2)

    def run(self):
        buf = b''
        while not self.stop:
            try:
                d = self.sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not d:
                break
            self.raw += d
            buf += d
            buf = self.parse(buf)

    def parse(self, buf):
        while buf:
            buf = buf.lstrip(b'\xfe')
            if not buf:
                break
            if buf[0] == 0x68:
                if len(buf) < 10 or len(buf) < 12 + buf[9]:
                    break
                n = 12 + buf[9]
                self.dlt(buf[:n])
                buf = buf[n:]
                continue
            if len(buf) < 2:
                break
            fc = buf[1]
            if 1 <= fc <= 6:
                need = 8
            elif fc in (15, 16):
                if len(buf) < 7:
                    break
                need = 9 + buf[6]
            else:
                buf = buf[1:]
                continue
            if len(buf) < need:
                break
            f = buf[:need]
            if bc.crc16(f[:-2]) != struct.unpack('<H', f[-2:])[0]:
                buf = buf[1:]
                continue
            buf = buf[need:]
            self.modbus(f)
        return buf

    def send(self, b):
        try:
            self.sock.sendall(b)
        except OSError:
            pass

    def modbus(self, f):
        unit, fc = f[0], f[1]
        start, count = struct.unpack('>HH', f[2:6])
        self.log.append((time.time(), unit, fc, start, count))
        if unit != 5:
            return  # unit 6 is dead, other units do not exist
        if fc in (1, 2):
            bits = [self.coils.get(start + i, 0) for i in range(count)]
            data = bytes(sum(b << k for k, b in enumerate(bits[i:i + 8])) for i in range(0, count, 8))
            self.send(bc.rtu(bytes([unit, fc, len(data)]) + data))
        elif fc in (3, 4):
            data = b''.join(struct.pack('>H', self.regs.get(start + i, 0)) for i in range(count))
            self.send(bc.rtu(bytes([unit, fc, len(data)]) + data))
        elif fc == 5:
            self.coils[start] = 1 if count == 0xFF00 else 0
            self.send(f)
        elif fc == 6:
            self.regs[start] = count
            self.send(f)
        elif fc == 15:
            for i in range(count):
                self.coils[start + i] = f[7 + i // 8] >> (i % 8) & 1
            self.send(bc.rtu(f[:6]))
        elif fc == 16:
            for i in range(count):
                self.regs[start + i] = struct.unpack('>H', f[7 + 2 * i:9 + 2 * i])[0]
            self.send(bc.rtu(f[:6]))

    def dlt(self, f):
        di = sum(((f[10 + i] - 0x33) & 0xFF) << (8 * i) for i in range(4))
        self.log.append((time.time(), 'dlt', di, 0, 0))
        data = self.meter.get(di)
        if data is None:
            return
        body = bytes([0x68]) + f[1:7] + bytes([0x68, 0x91, 4 + len(data)]) + f[10:14] + bytes((b + 0x33) & 0xFF for b in data)
        self.send(body + bytes([sum(body) & 0xFF, 0x16]))

    def close(self):
        self.stop = True
        try:
            self.sock.close()
        except OSError:
            pass


class Stream(threading.Thread):
    """A client of port1's TCP server: what arrives, with the time of each piece."""

    def __init__(self, port=23, rcvbuf=0):
        super().__init__(daemon=True)
        self.sock = socket.socket()
        if rcvbuf:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
        self.sock.connect((GW, port))
        self.sock.settimeout(0.2)
        self.data = b''
        self.pieces = []
        self.stop = False
        self.slow = 0

    def run(self):
        while not self.stop:
            if self.slow > 1:  # paused
                time.sleep(0.05)
                continue
            try:
                d = self.sock.recv(65536 if not self.slow else 512)
            except socket.timeout:
                continue
            except OSError:
                break
            if not d:
                break
            self.pieces.append((time.time(), d))
            self.data += d
            if self.slow:
                time.sleep(self.slow)

    def objects(self):
        """The JSON objects in the stream, (end time, object, text length), and the bytes between them."""
        dec, out, rest, i, s = json.JSONDecoder(), [], b'', 0, self.data.decode('latin-1')
        while i < len(s):
            j = s.find('{', i)
            if j < 0:
                rest += s[i:].encode('latin-1')
                break
            rest += s[i:j].encode('latin-1')
            try:
                o, k = dec.raw_decode(s, j)
            except ValueError:
                rest += s[j:].encode('latin-1')
                break
            out.append((o, k - j))
            i = k
        return out, rest

    def close(self):
        self.stop = True
        self.sock.close()


# ---- configuration --------------------------------------------------------------------------------

def plc(**kw):
    d = {'name': 'plc', 'addr': '5', 'uart': 1, 'poll': 1000, 'merge': True, 'nodes': [
        {'name': 't', 'reg': 10, 'dtype': 'int16', 'formula': '=%s/10', 'onChange': True, 'range': 2, 'timeout': 500},
        {'name': 'c2', 'reg': 11, 'dtype': 'uint16', 'timeout': 500},
        {'name': 'f', 'reg': 12, 'dtype': 'float32_abcd', 'timeout': 500},
        {'name': 'c', 'fc': 1, 'reg': 3, 'dtype': 'bool', 'timeout': 500}]}
    d.update(kw)
    return d


def meter(**kw):
    d = {'name': 'meter', 'protocol': 'dlt645', 'addr': METER, 'uart': 1, 'poll': 1000,
         'nodes': [{'name': 'v', 'di': '02010100', 'formula': '=%s/10', 'timeout': 500}]}
    d.update(kw)
    return d


def tcpdev(**kw):
    d = {'name': 'tcp', 'source': 'eth', 'ip': WIN, 'ethPort': MBTCP, 'addr': '1', 'poll': 1000,
         'nodes': [{'name': 'h', 'reg': 0, 'dtype': 'uint16', 'timeout': 1000}]}
    d.update(kw)
    return d


def io(**kw):
    d = {'name': 'x', 'source': 'io', 'poll': 100, 'nodes': [{'ioPoint': 'DO1'}, {'ioPoint': 'DO2'}]}
    d.update(kw)
    return d


def virt(**kw):
    d = {'name': 'y', 'source': 'virtual', 'addr': '200', 'poll': 500,
         'nodes': [{'name': 's', 'formula': '=%s+%s,t,f', 'dtype': 'float32_abcd'}]}
    d.update(kw)
    return d


TEMPLATE = {'t': 't', 'f': 'f', 'c': 'c', 'v': 'v', 'h': 'h', 'do1': 'DO1', 's': 's', 'k': 'const',
            'sn': 'sys_sn', 'mac': 'sys_mac', 'ts': 'sys_time', 'ut': 'sys_unix_time'}


def setup(devices, report=None, links=None, port1=None, port2=None, sys_extra=None):
    """The Edge model and both ports, then a restart. Port2 is the bus slave's link (TCP server on 24)."""
    _, rep = w.get_json('/api/config/edgeReport')
    rep.update({'channel': 1, 'reporting': 0, 'remoteAccess': 0, 'periodic': True, 'interval': 2, 'scheduled': False,
                'failurePlaceholder': True, 'placeholder': 'null', 'quoteValues': False, 'template': json.dumps(TEMPLATE)})
    rep.update(report or {})
    posts = [('edgeDevices', devices), ('edgeReport', rep), ('edgeLinkage', links or []), ('edge', {'enabled': True})]
    for name, port, extra in (('port1', 1, port1), ('port2', 2, port2)):
        _, p = w.get_json('/api/config/' + name)
        p.update(bc.base(listenPort=23 if port == 1 else 24, baudRate=115200))
        p.update(extra or {})
        posts.append((name, p))
    _, sysc = w.get_json('/api/config/system')
    sysc.update({'ntp': 0, 'bufferOffline': 0, 'idleRestart': 0})
    sysc.update(sys_extra or {})
    posts.append(('system', sysc))
    ok = True
    for name, body in posts:
        s, _, d = w.req('POST', '/api/config/' + name, body=json.dumps(body).encode())
        if s != 200:
            print('     %s: %d %s' % (name, s, d[:120]))
            ok = False
    ok = ok and w.restart()
    time.sleep(3)
    return ok


def status():
    _, st = w.get_json('/api/status')
    return st['edge']['state'] if st else ''


def dos():
    _, d = w.get_json('/api/io')
    return d['do'] if d else []


def set_do(i, on):
    w.req('POST', '/api/io', body=json.dumps({'do': i, 'value': on}).encode())


def log_text():
    s, _, d = w.req('GET', '/api/log')
    return d.decode(errors='replace').split('# Since the last start', 1)[-1] if s == 200 else ''


def wait(cond, secs, step=0.1):
    end = time.time() + secs
    while time.time() < end:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return cond()


def start_all(**peer_args):
    """The bus slave, the Modbus TCP device, and a client of port1."""
    slave = BusSlave()
    slave.start()
    tcp = bc.peer('tcpserver', port=MBTCP, secs=peer_args.pop('secs', 40), modbus=1, **peer_args)
    return slave, tcp


def cadence_ok(st, rs, every):
    """Reports per second over the window: TCP may split one report or join two in a segment, so the count
    over the span is what is checked, not the gaps between segments."""
    span = st.pieces[-1][0] - st.pieces[0][0] if len(st.pieces) > 1 else 0
    return span >= 3 * every and abs((len(rs) - 1) - span / every) <= 1.5, (len(rs), round(span, 1))


def reports(st):
    return [o for o, _ in st.objects()[0] if 'k' in o or 'rw_prot' not in o]


# ---- cases ---------------------------------------------------------------------------------------------

def case_basic():
    check(setup([plc(), meter(), tcpdev(), io(), virt()], report={'reporting': 1}), 'set up Edge: RTU, DL/T645, Modbus TCP, IO, virtual')
    slave, tcp = start_all(secs=20)
    st = Stream()
    st.start()
    time.sleep(10)
    rs = reports(st)
    # while the slaves still answer: once they close, the points fail one by one
    full = wait(lambda: re.match(r'RUNNING (\d+)/\1$', status()), 5, 0.5)  # a late packet on the bench's WSL path can fail a read
    status_now = status()
    st.close()
    bc.wait_peer(tcp, 1)
    slave.close()
    r = rs[-1] if rs else {}
    _, s = w.get_json('/api/status')
    check(r.get('t') == 25 and r.get('f') == 10 and r.get('c') == 1 and r.get('v') == 220.1 and r.get('h') == 0x1234 and r.get('s') == 35,
          'a periodic report: Modbus RTU (scaled, float32, a coil), DL/T645, Modbus TCP, virtual (%s)' % r)
    check(r.get('do1') in (0, 1), 'the IO point DO1 in it (%s)' % r.get('do1'))
    check(r.get('k') == 'const' and r.get('sn') == s['system']['sn'] and r.get('mac') == s['system']['mac'],
          'template constants kept, sys_sn and sys_mac filled')
    # No real time: the device clock (PR #28, spec §10.4): 2020-01-01 00:00 local at power-on plus the uptime, sys_time
    # as UTC text, sys_unix_time its seconds (before the real clock, so before 2021).
    ts, ut = r.get('ts'), r.get('ut')
    check(isinstance(ts, str) and ts[:4] in ('2019', '2020') and isinstance(ut, (int, float)) and 1577750400 <= ut < 1609459200,
          'no real time: sys_time and sys_unix_time carry the device clock (%r, %r)' % (ts, ut))
    check(full, 'status: every point read (%s)' % status_now)
    fc3 = [(a, n) for _, u, fc, a, n in slave.log if u == 5 and fc == 3]
    check(fc3 and all(x == (10, 4) for x in fc3), 'batch reads: t, c2, f in one request, FC3 10..13 (%s)' % fc3[:3])
    u5 = [(t, fc) for t, u, fc, a, n in slave.log if u == 5]
    gaps = [round(b[0] - a[0], 2) for a, b in zip(u5, u5[1:])]
    pairs = [(a[1], b[1]) for a, b in zip(u5, u5[1:])]
    good = [g for g, (x, y) in zip(gaps, pairs) if 0.85 <= g <= 1.4 and x != y]
    # The slave's log is stamped on the PC: TCP through WSL's NAT now and then delays a packet by seconds.
    check(len(gaps) >= 6 and len(good) >= 0.8 * len(gaps) and sorted(gaps)[len(gaps) // 2] <= 1.4,
          "the polling interval: one block, then 1 s, then the device's next block (%d of %d gaps: %s)" % (len(good), len(gaps), gaps))
    ok, seen = cadence_ok(st, rs, 2)
    check(ok, 'periodic: every 2 s (%d reports over %s s)' % seen)


def case_change():
    links = [{'name': 'hot', 'enable': True, 'cond': 2, 'point': 't', 'lower': 50, 'output': 'DO2', 'action': 2,
              'cycle': 100, 'minInterval': 500, 'desc': 'too hot'}]
    check(setup([plc(), io()], report={'reporting': 1, 'interval': 30, 'template': json.dumps({'t': 't'})}, links=links),
          'set up: on-change t (range 2), periodic 30 s, linkage t >= 50 closes DO2')
    set_do(1, False)
    slave, tcp = start_all(secs=5)
    st = Stream()
    st.start()
    time.sleep(4)
    n0 = len(reports(st))
    slave.regs[10] = 600  # t = 60
    t0 = time.time()
    got = wait(lambda: len(reports(st)) > n0, 5)
    dt = time.time() - t0
    on = wait(lambda: dos()[1] is True if dos() else False, 3)
    st.close()
    slave.close()
    bc.wait_peer(tcp, 1)
    check(got and reports(st)[-1].get('t') == 60 and dt < 3, 'report on change, %.1f s after it, before the next periodic one' % dt)
    check(on, 'linkage: t >= 50 closes DO2')
    check('linkage hot fired (t = 60): too hot' in log_text(), 'linkage logs its description')


def case_schedule():
    check(setup([plc()], report={'reporting': 1, 'periodic': False, 'scheduled': True, 'schedule': 2,
                                 'template': json.dumps({'t': 't', 'ts': 'sys_time', 'ut': 'sys_unix_time'})}),
          'set up: a scheduled report every minute')
    _, sysc = w.get_json('/api/config/system')
    tz = int(sysc.get('timezone', 0))
    now = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(hours=tz)
    t = now.replace(second=50)
    if t < now:
        t += datetime.timedelta(minutes=1)
    slave, tcp = start_all(secs=5)
    st = Stream()
    st.start()
    w.req('POST', '/api/time', body=json.dumps({'time': t.strftime('%Y-%m-%d %H:%M:%S')}).encode())
    # The minute the clock was set in is due at once (the ESP32's schedule_due); the next comes at :00.
    got = wait(lambda: [r for r in reports(st) if str(r.get('ts', '')).endswith(':00')], 25)
    first = reports(st)[:1]
    st.close()
    slave.close()
    bc.wait_peer(tcp, 1)
    r = got[0] if got else {}
    utc = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d %H:')
    check(got and isinstance(r.get('ut'), (int, float)) and r['ts'].startswith(utc[:11]),
          'clock set: a report at the minute, sys_unix_time a number, sys_time UTC text (%s, first %s)' % (r, first))
    check(got and abs(datetime.datetime.strptime(r['ts'], '%Y-%m-%d %H:%M:%S').replace(tzinfo=datetime.timezone.utc).timestamp() - r['ut']) < 1,
          'sys_time and sys_unix_time agree')


def connect(port=23):
    """A client of port1. TCP_NODELAY: pieces go at once; with Nagle the PC holds a piece until lwIP's delayed
    ACK, past the 200 ms after which a partial request goes on to RS485 (Plan 08c ruling 3)."""
    s = socket.create_connection((GW, port), 5)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return s


def ask(sock, req, secs=2, want=1):
    sock.sendall(req)
    return bc.recv_some(sock, secs, want)


def rtu_req(unit, fc, a, b):
    return bc.rtu(struct.pack('>BBHH', unit, fc, a, b))


def case_remote():
    devs = [plc(mapEn=True, mapAddr=10, nodes=plc()['nodes'][:2] + [plc()['nodes'][2], plc()['nodes'][3],
                                                                    {'name': 'c5', 'fc': 1, 'reg': 5, 'dtype': 'bool', 'timeout': 500}]),
            meter(mapEn=True, mapAddr=11, nodes=[dict(meter()['nodes'][0], mapEn=True, mapAddr=50, formula='=%s/10')]),
            {'name': 'dead', 'addr': '6', 'uart': 1, 'poll': 2000, 'nodes': [{'name': 'z', 'reg': 0, 'timeout': 300}]},
            io(mapEn=True, mapAddr=120, nodes=[{'ioPoint': 'DO1'}, {'ioPoint': 'DO2', 'mapEn': True, 'mapAddr': 10}]),
            virt(nodes=[dict(virt()['nodes'][0], mapEn=True, mapAddr=30)])]
    devs[0]['nodes'][0] = dict(devs[0]['nodes'][0], mapEn=True, mapAddr=100)
    # rw_prot JSON
    check(setup(devs, report={'remoteAccess': 1, 'remoteWrite': True, 'remoteFormat': 2}), 'set up: remote access, rw_prot JSON')
    slave, tcp = start_all(secs=60)
    time.sleep(4)
    s = connect()
    req = {'rw_prot': {'Ver': '1.0.1', 'dir': 'down', 'id': '1', 'r_data': [{'name': 't'}]}}
    got = ask(s, json.dumps(req).encode(), 3, 20)
    check(b'"name":"t","value":"25","err":"0"' in got, 'rw_prot read (%s)' % got[:120])
    req = {'rw_prot': {'Ver': '1.0.1', 'dir': 'down', 'id': '2', 'w_data': [
        {'name': 'c2', 'value': '9'}, {'name': 'c', 'value': '0'}, {'name': 'DO1', 'value': '1'}, {'name': 'v', 'value': '5'}]}}
    got = ask(s, json.dumps(req).encode(), 4, 20)
    try:
        errs = [x['err'] for x in json.loads(got)['rw_prot']['w_data']]
    except (ValueError, KeyError):
        errs = []
    check(errs == ['0', '0', '0', '1'], 'rw_prot write: register, coil, DO; DL/T645 is read-only (%s)' % errs)
    check(slave.regs.get(11) == 9 and slave.coils.get(3) == 0 and (dos() or [0])[0] is True, 'the writes reached the slave and DO1')
    raw0 = len(slave.raw)
    s.sendall(b'{"x":1}')
    time.sleep(1.5)
    check(b'{"x":1}' in slave.raw[raw0:], 'other JSON on the socket goes on to RS485')
    check(b'rw_prot' not in slave.raw, "Edge's own requests never reach RS485")
    s.close()
    slave.close()
    bc.wait_peer(tcp, 1)

    # Modbus RTU, remoteWrite on
    check(setup(devs, report={'remoteAccess': 1, 'remoteWrite': True, 'remoteFormat': 0}), 'set up: remote access, Modbus RTU')
    slave, tcp = start_all(secs=60)
    set_do(0, True)
    set_do(1, False)
    time.sleep(5)
    s = connect()
    r = ask(s, rtu_req(10, 3, 100, 1), 2, 7)
    check(r == bc.rtu(bytes([10, 3, 2, 0, 250])), 'mapped slave 10, t at register 100: its raw value (%s)' % r.hex())
    r = ask(s, rtu_req(10, 3, 11, 1), 2, 7)
    check(r == bc.rtu(bytes([10, 3, 2, 0, 7])), 'an unmapped point stays at its own register 11 (%s)' % r.hex())
    r = ask(s, rtu_req(200, 4, 30, 2), 2, 9)
    check(r == bc.rtu(bytes([200, 4, 4]) + struct.pack('>f', 35.0)), 'the virtual device: slave 200, FC4 30 = 35.0 (%s)' % r.hex())
    r = ask(s, rtu_req(200, 3, 30, 2), 2, 5)
    check(r[:3] == bytes([200, 0x83, 2]), 'virtual points are FC4 only: FC3 refused (%s)' % r.hex())
    r = ask(s, rtu_req(120, 1, 0, 1), 2, 6)
    check(r == bc.rtu(bytes([120, 1, 1, 1])), 'the IO device at slave 120: DO1 at coil 0, on (%s)' % r.hex())
    r = ask(s, rtu_req(120, 1, 10, 1), 2, 6)
    check(r == bc.rtu(bytes([120, 1, 1, 0])), 'DO2 mapped to coil 10, off (%s)' % r.hex())
    r = ask(s, rtu_req(10, 4, 100, 1), 2, 5)
    check(r[:3] == bytes([10, 0x84, 2]), 'function codes kept apart: FC4 at a holding register refused (%s)' % r.hex())
    r = ask(s, rtu_req(11, 3, 50, 1), 2, 7)
    check(r == bc.rtu(bytes([11, 3, 2, 0, 220])), 'the DL/T645 meter at slave 11, its point at register 50: 220 (%s)' % r.hex())
    body = struct.pack('>BBHHB', 10, 16, 12, 2, 4) + struct.pack('>f', 12.5)
    r = ask(s, bc.rtu(body), 3, 8)
    check(r == bc.rtu(body[:6]) and slave.regs.get(12) == 0x4148 and slave.regs.get(13) == 0,
          'FC16 through the mapping writes the float32 point f to the device (%s)' % r.hex())
    r = ask(s, bc.rtu(struct.pack('>BBHH', 10, 6, 13, 1)), 2, 5)
    check(r[:3] == bytes([10, 0x86, 2]), 'FC6 on half of a float32 point is refused (%s)' % r.hex())
    before = slave.coils.get(3)
    r = ask(s, bc.rtu(struct.pack('>BBHHBB', 10, 15, 3, 3, 1, 0b000)), 3, 5)  # coils 3..5, 4 missing: zeros would show
    check(r[:3] == bytes([10, 0x8F, 2]) and slave.coils.get(3) == before == 1 and not [x for x in slave.log if x[2] == 15],
          'FC15 across a missing coil: exception 2, nothing written (%s)' % r.hex())
    time.sleep(2.5)
    r = ask(s, rtu_req(6, 3, 0, 1), 2, 5)
    check(r[:3] == bytes([6, 0x83, 0x0B]), 'a point that never answered: exception 0x0B (%s)' % r.hex())
    s.close()
    slave.close()
    bc.wait_peer(tcp, 1)

    # remoteWrite off
    check(setup(devs, report={'remoteAccess': 1, 'remoteWrite': False, 'remoteFormat': 0}), 'set up: remote writes off')
    slave, tcp = start_all(secs=30)
    time.sleep(4)
    s = connect()
    r = ask(s, bc.rtu(struct.pack('>BBHH', 10, 6, 11, 3)), 2, 5)
    check(r[:3] == bytes([10, 0x86, 1]) and slave.regs.get(11) == 7, 'remote writes off: illegal function (%s)' % r.hex())
    r = ask(s, bc.rtu(struct.pack('>BBHH', 120, 5, 0, 0)), 2, 5)
    check(r[:3] == bytes([120, 0x85, 1]), 'an IO output write refused too (%s)' % r.hex())
    s.close()
    slave.close()
    bc.wait_peer(tcp, 1)


def case_split():
    devs = [plc(mapEn=True, mapAddr=10)]
    check(setup(devs, report={'remoteAccess': 1, 'remoteFormat': 1}), 'set up: remote access, Modbus TCP framing')
    slave, tcp = start_all(secs=30)
    time.sleep(4)
    s = connect()
    a = bc.mbap(1, bytes([10, 3, 0, 11, 0, 1]))
    s.sendall(a[:3])
    time.sleep(0.05)
    s.sendall(a[3:8])
    time.sleep(0.05)
    r = ask(s, a[8:], 2, 11)
    check(r == bc.mbap(1, bytes([10, 3, 2, 0, 7])), 'an MBAP request in three pieces: answered (%s)' % r.hex())
    two = bc.mbap(2, bytes([10, 3, 0, 11, 0, 1])) + bc.mbap(3, bytes([10, 3, 0, 10, 0, 1]))
    r = ask(s, two, 2, 22)
    check(r == bc.mbap(2, bytes([10, 3, 2, 0, 7])) + bc.mbap(3, bytes([10, 3, 2, 0, 250])), 'two in one segment: both answered (%s)' % r.hex())
    s.close()
    check(setup(devs, report={'remoteAccess': 1, 'remoteFormat': 0}), 'set up: RTU framing')
    time.sleep(3)
    slave.close()
    slave, _ = BusSlave(), None
    slave.start()
    time.sleep(3)
    s = connect()
    q = rtu_req(10, 3, 11, 1)
    s.sendall(q[:3])
    time.sleep(0.05)
    r = ask(s, q[3:], 2, 7)
    check(r == bc.rtu(bytes([10, 3, 2, 0, 7])), 'an RTU request in two pieces: answered (%s)' % r.hex())
    r = ask(s, q + rtu_req(10, 3, 10, 1), 2, 14)
    check(r == bc.rtu(bytes([10, 3, 2, 0, 7])) + bc.rtu(bytes([10, 3, 2, 0, 250])), 'two RTU requests in one segment (%s)' % r.hex())
    s.close()
    slave.close()
    bc.wait_peer(tcp, 1)


def case_gwrtu():
    """Codex 08c code review: on a Modbus gateway port, Edge's RTU requests are framed too (their own accumulator),
    and what is not Edge's goes on to the gateway."""
    check(setup([plc(mapEn=True, mapAddr=10)], report={'remoteAccess': 1, 'remoteFormat': 0}, port1={'socketMode': 1}),
          'set up: port1 a Modbus gateway, Edge remote access RTU')
    slave = BusSlave()
    slave.start()
    time.sleep(3)
    s = connect()
    q = rtu_req(10, 3, 11, 1)
    s.sendall(q[:3])
    time.sleep(0.05)
    r = ask(s, q[3:], 2, 7)
    check(r == bc.rtu(bytes([10, 3, 2, 0, 7])), 'an RTU request for Edge in two pieces: answered (%s)' % r.hex())
    r = ask(s, q + rtu_req(10, 3, 10, 1), 2, 14)
    check(r == bc.rtu(bytes([10, 3, 2, 0, 7])) + bc.rtu(bytes([10, 3, 2, 0, 250])), 'two in one segment: both (%s)' % r.hex())
    r = ask(s, bc.mbap(7, bytes([5, 3, 0, 11, 0, 1])), 3, 11)
    check(r == bc.mbap(7, bytes([5, 3, 2, 0, 7])), "Modbus TCP for the bus goes on to the gateway (%s)" % r.hex())
    s.close()
    slave.close()


def case_contention():
    check(setup([plc(poll=200, mapEn=True, mapAddr=10)], report={'remoteAccess': 1, 'remoteFormat': 1},
                port1={'socketMode': 1}), 'set up: port1 a Modbus gateway, Edge polling unit 5 every 200 ms')
    slave, tcp = start_all(secs=40)
    time.sleep(4)
    s = connect()
    good, leaked = 0, b''
    for i in range(20):
        r = ask(s, bc.mbap(100 + i, bytes([5, 3, 0, 11, 0, 1])), 2, 11)
        good += r == bc.mbap(100 + i, bytes([5, 3, 2, 0, 7]))
        leaked += r[11:]
    time.sleep(1)
    extra = bc.recv_some(s, 1, 1)
    state = status()  # while the slave still answers
    s.close()
    slave.close()
    bc.wait_peer(tcp, 1)
    check(good == 20, 'contention: every network request got its own reply (%d/20)' % good)
    check(not leaked and not extra, 'contention: no Edge poll reply leaked to the network client')
    check(re.match(r'RUNNING (\d+)/\1', state), 'contention: Edge kept every point (%s)' % state)


def case_dead():
    dead = {'name': 'dead', 'addr': '6', 'uart': 1, 'poll': 1000,
            'nodes': [{'name': 'd%d' % i, 'reg': i * 10, 'timeout': 1000} for i in range(10)]}
    links = [{'name': 'mirror', 'enable': True, 'cond': 0, 'point': 'DO1', 'output': 'DO2', 'cycle': 100, 'minInterval': 500}]
    tmpl = {'t': 't', 'h': 'h', 'd0': 'd0', 'd9': 'd9'}
    quiet = plc(nodes=[dict(n, onChange=False) for n in plc()['nodes']])  # periodic reports only: the cadence is counted
    check(setup([quiet, dead, tcpdev(), io()], report={'reporting': 1, 'interval': 2, 'template': json.dumps(tmpl)}, links=links),
          'set up: a dead device with 10 nodes of 1 s on the bus')
    set_do(0, False)
    slave, tcp = start_all(secs=50)
    st = Stream()
    st.start()
    time.sleep(8)
    set_do(0, True)
    t0 = time.time()
    on = wait(lambda: dos()[1] is True if dos() else False, 2, 0.05)
    follow = time.time() - t0
    time.sleep(25)
    rs = reports(st)
    st.close()
    ev = bc.wait_peer(tcp, 1)
    slave.close()
    check(on and follow <= 0.6, 'dead device: linkage still follows within 0.5 s (%.2f s)' % follow)
    ok, seen = cadence_ok(st, rs, 2)
    check(ok, 'dead device: periodic reports keep a 2 s cadence (%d reports over %s s)' % seen)
    check(rs and rs[-1].get('d0', 1) is None and rs[-1].get('t') == 25, 'dead device: its nodes are the placeholder (%s)' % (rs[-1] if rs else '-'))
    reqs = [e for e in ev if e.get('ev') == 'req']
    check(len(reqs) >= 25, 'dead device: the Modbus TCP device keeps being polled (%d requests)' % len(reqs))
    per = {}
    for t, u, fc, a, n in slave.log:
        if u == 6:
            per.setdefault(a, []).append(t)
    close = [a for a, ts in per.items() if any(b - x < 9 for x, b in zip(ts, ts[1:]))]
    check(len(per) == 10 and not close, 'dead device: each node asked once per round, no retries (%s)' % {a: len(v) for a, v in per.items()})


def case_late():
    dev = tcpdev(poll=500, merge=False, nodes=[{'name': 'h', 'reg': 0, 'timeout': 1000}, {'name': 'h1', 'reg': 1, 'timeout': 1000}])
    check(setup([dev], report={'reporting': 1, 'interval': 1, 'template': json.dumps({'h': 'h', 'h1': 'h1'})}),
          'set up: a Modbus TCP device that answers one request late')
    tcp = bc.peer('tcpserver', port=MBTCP, secs=25, modbus=1, addr=1, late=1, late_ms=1500)
    st = Stream()
    st.start()
    time.sleep(20)
    rs = reports(st)
    st.close()
    bc.wait_peer(tcp, 1)
    bad = [r for r in rs if (r.get('h') not in (None, 0x1234)) or (r.get('h1') not in (None, 0x1235))]
    check(len(rs) > 10 and not bad and rs[-1] == {'h': 0x1234, 'h1': 0x1235},
          'a late reply is discarded: values stay with their own points (%d reports, %d wrong)' % (len(rs), len(bad)))


def case_shortreport():
    check(setup([plc()], report={'reporting': 1, 'periodic': False, 'template': json.dumps({'t': 't'})},
                port1={'workMode': 1, 'socketMode': 2, 'shortConnectionTime': 3, 'remoteHost': WIN, 'remotePort': 9100}),
          'set up: port1 a TCP client with a short connection, on-change reports only')
    slave, tcp = start_all(secs=5)
    p = bc.peer('tcpserver', port=9100, secs=20)
    time.sleep(3)
    slave.regs[10] = 300
    ev = bc.wait_peer(p, 1)
    slave.close()
    bc.wait_peer(tcp, 1)
    data = bc.data_of(ev)
    acc = [e for e in ev if e.get('ev') == 'accept']
    closes = [e for e in ev if e.get('ev') == 'close']
    check(acc and b'"t":' in data and data.strip().startswith(b'{'), 'a pending report dials the short connection, no serial data at all (%r)' % data[:60])
    life = [c['t'] - a['t'] for a, c in zip(acc, closes)]  # each dial with its own close
    check(life and len(closes) == len(acc) and all(2000 <= x <= 7000 for x in life), 'and each connection closes after the idle time (%s ms)' % life)


def case_partial():
    check(setup([plc(mapEn=True, mapAddr=10)], report={'remoteAccess': 1, 'remoteFormat': 0}), 'set up: remote RTU on port1')
    slave = BusSlave()
    slave.start()
    time.sleep(3)
    s = connect()
    raw0 = len(slave.raw)
    s.sendall(bytes([10, 3, 0]))
    t0 = time.time()
    got = wait(lambda: bytes([10, 3, 0]) in slave.raw[raw0:], 2, 0.02)
    dt = time.time() - t0
    s.close()
    slave.close()
    check(got and 0.15 <= dt <= 0.6, 'a partial request, then silence: on to RS485 after about 200 ms (%.2f s)' % dt)


def case_bigreport():
    # The template is at most 2048 characters (the schema): 70 slots of sys_sn (12 characters) render about 2.3 KB,
    # over the bridge's 1.5 KB frame.
    big = {'k_long_name_%03d' % i: 'sys_sn' for i in range(70)}
    big['t'] = 't'
    check(setup([plc()], report={'reporting': 1, 'interval': 1, 'remoteAccess': 1, 'remoteFormat': 2, 'template': json.dumps(big, separators=(',', ':'))}),
          'set up: a report of about 2.3 KB every second, rw_prot on the same socket')
    slave = BusSlave()
    slave.start()
    time.sleep(3)
    st = Stream(rcvbuf=1024)
    st.start()
    time.sleep(3)
    for i in range(5):  # serial frames from the bus to port1, between reports
        slave.send(b'SER%02d' % i)
        time.sleep(0.3)
    # Stop reading for 6 s: lwIP's send buffer (TCP_SND_BUF 5840) and the PC's receive buffer fill, and the bridge
    # holds a report half sent. Then a request, and reading again.
    st.slow = 6
    time.sleep(1)
    rcv = st.sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
    time.sleep(6)
    before = len(st.data)
    req = {'rw_prot': {'Ver': '1.0.1', 'dir': 'down', 'id': '9', 'r_data': [{'name': 't'}]}}
    st.sock.sendall(json.dumps(req).encode())
    st.slow = 0
    time.sleep(4)
    st.close()
    objs, rest = st.objects()
    reps = [(o, n) for o, n in objs if 'k_long_name_000' in o]
    replies = [o for o, n in objs if 'rw_prot' in o]
    at = st.data.find(b'"rw_prot"')
    ahead = (at - before) if at > 0 else -1  # report bytes that had to go out before the reply
    check(ahead > 5840 + rcv, 'the reply waited for %d report bytes, more than lwIP and the PC hold (%d): a report was half sent in the bridge'
          % (ahead, 5840 + rcv))
    check(len(reps) >= 5 and all(len(o) == 71 and n > 2000 for o, n in reps), 'TCP: whole reports of %d bytes, one byte stream each' % (reps[0][1] if reps else 0))
    check(rest.replace(b' ', b'') == b''.join(b'SER%02d' % i for i in range(5)), 'serial frames between them arrive whole and in order (%r)' % rest[:60])
    check(len(replies) == 1, 'a remote request while a report is half sent: the report whole, then the reply')
    # UDP: one datagram
    check(setup([plc()], report={'reporting': 1, 'interval': 1, 'template': json.dumps(big, separators=(',', ':'))},
                port1={'workMode': 0, 'remoteHost': WIN, 'remotePort': 9101, 'localPort': 9101}), 'set up: port1 a UDP client')
    p = bc.peer('udp', port=9101, secs=8)
    ev = bc.wait_peer(p, 1)
    slave.close()
    d = [bytes.fromhex(e['hex']) for e in ev if e.get('ev') == 'data']
    check(d and all(len(x) > 2000 and json.loads(x).get('k_long_name_069') for x in d), 'UDP: one datagram per report (%s)' % [len(x) for x in d][:4])
    # the 26 KB limit
    _, before = w.get_json('/api/config/edgeReport')
    over = dict(before, template=json.dumps({'a%03d' % i: 't' for i in range(120)}))
    s, _, body = w.req('POST', '/api/config/edgeReport', body=json.dumps(over).encode())
    _, after = w.get_json('/api/config/edgeReport')
    # Plan 09b: the store keeps what it is sent (the UI refuses it, rules.ts); at start-up edge_rt turns reports off.
    w.restart()
    time.sleep(3)
    off = re.search(r'the report template needs more than \d+ bytes: reports are off', log_text())
    check(s == 200 and after['template'] == over['template'] and off,
          'a template needing over 26 KB: stored as sent (%d), at start-up reports are off' % s)
    w.req('POST', '/api/config/edgeReport', body=json.dumps(before).encode())


def case_mqtt():
    """Plan 09: channel 0. Reports go into MQTT's outbox, rw_prot comes on the request topic and is answered on the reply
    topic (mosquitto in WSL behind win_peer.py mqttrelay, as in mqtt_check.py)."""
    import mqtt_check as mc
    import tls_check as tc
    tc.make_pki()
    brk = mc.main_broker().start()
    try:
        r = mc.relay(60)
        ok = mc.post('mqtt', mc.mqtt_cfg()) and mc.post('mqttPublish', []) and mc.post('mqttSubscribe', [])
        # A large report the 2048-character template allows: 70 slots of sys_sn, about 2.3 KB.
        big = {'k_long_name_%03d' % i: 'sys_sn' for i in range(70)}
        big['t'] = 't'
        check(ok and setup([plc()], report={'channel': 0, 'reporting': 1, 'interval': 2, 'reportTopic': 'edge/report', 'reportQos': 1,
                                             'remoteAccess': 1, 'remoteWrite': True, 'remoteFormat': 2, 'requestTopic': 'edge/req',
                                             'requestQos': 1, 'replyTopic': 'edge/reply', 'replyQos': 1,
                                             'template': json.dumps(big, separators=(',', ':'))}),
              'set up: channel 0 (MQTT), a 2.3 KB report every 2 s, rw_prot on edge/req')
        slave = BusSlave()
        slave.start()
        sub = mc.Sub('edge/#', qos=1)
        time.sleep(8)
        reps = [json.loads(p) for p in sub.payloads('edge/report')]
        sizes = [len(p) for p in sub.payloads('edge/report')]
        check(len(reps) >= 3 and all(len(o) == 71 and o.get('k_long_name_069') for o in reps) and reps[-1].get('t') == 25,
              'reports reach the report topic whole (%d of %s bytes, t=%s)' % (len(reps), sizes[:1], reps[-1].get('t') if reps else None))
        req = {'rw_prot': {'Ver': '1.0.1', 'dir': 'down', 'id': '7', 'r_data': [{'name': 't'}]}}
        mc.pub('edge/req', json.dumps(req).encode(), qos=1)
        check(wait(lambda: any(b'"name":"t","value":"25","err":"0"' in p for p in sub.payloads('edge/reply')), 5),
              'rw_prot read over the request topic, the reply on the reply topic (%s)' % sub.payloads('edge/reply')[:1])
        req = {'rw_prot': {'Ver': '1.0.1', 'dir': 'down', 'id': '8', 'w_data': [{'name': 'c2', 'value': '9'}]}}
        mc.pub('edge/req', json.dumps(req).encode(), qos=1)
        check(wait(lambda: slave.regs.get(11) == 9, 5), 'rw_prot write over MQTT reached the slave (%s)' % slave.regs.get(11))
        check('Plan 09' not in status() and status().startswith('RUNNING'), 'status: %s' % status())
        sub.stop()
        slave.close()
        bc.wait_peer(r, 1)
    finally:
        mc.post('mqtt', mc.mqtt_cfg(enabled=0, host=''))
        brk.stop()


def case_resync():
    """Outside review 3: once MQTT connects again, Edge sends the current values, also with on-change reports only and
    nothing changed (a change queued while away may have expired in the outbox, its baseline moved on)."""
    import mqtt_check as mc
    brk = mc.main_broker().start()
    slave = None
    try:
        r = mc.relay(25)
        ok = mc.post('mqtt', mc.mqtt_cfg(retryInterval=3)) and mc.post('mqttPublish', []) and mc.post('mqttSubscribe', [])
        check(ok and setup([plc()], report={'channel': 0, 'reporting': 1, 'periodic': False, 'reportTopic': 'edge/report',
                                            'reportQos': 1, 'template': json.dumps({'t': 't'})}),
              'set up: channel 0 (MQTT), on-change reports only, template {"t": "t"}')
        slave = BusSlave()
        slave.start()
        sub = mc.Sub('edge/report', qos=1)
        check(wait(lambda: sub.payloads('edge/report'), 20), 'the first report (t had no reported value)')
        bc.wait_peer(r, 1)  # the relay ends: MQTT is away
        check(wait(lambda: mc.state() != 'CONNECTED', 20), 'MQTT away (%s)' % mc.state())
        n0 = len(sub.payloads('edge/report'))
        r = mc.relay(30)
        check(wait(lambda: mc.state() == 'CONNECTED', 20), 'MQTT connected again')
        got = wait(lambda: len(sub.payloads('edge/report')) > n0, 10)
        last = sub.payloads('edge/report')[-1] if sub.payloads('edge/report') else b'{}'
        check(got and json.loads(last).get('t') == 25, 'after the reconnect, a report of the current values, nothing changed (%s)' % last)
        sub.stop()
        bc.wait_peer(r, 1)
    finally:
        if slave:
            slave.close()
        mc.post('mqtt', mc.mqtt_cfg(enabled=0, host=''))
        brk.stop()


def case_memory():
    nodes = lambda n, base, **kw: [dict({'name': '%s%d' % (base, i), 'reg': 20 + i, 'timeout': 300}, **kw) for i in range(n)]
    devs = [plc(nodes=nodes(80, 'p')), {'name': 'b2', 'addr': '9', 'uart': 2, 'poll': 2000, 'nodes': nodes(60, 'q')}]
    for k in range(5):
        devs.append(tcpdev(name='tcp%d' % k, nodes=nodes(8, 'e%d_' % k)))
    devs += [io(), virt(nodes=[{'name': 'v%d' % i, 'formula': '=%s+1,p0', 'dtype': 'float32_abcd'} for i in range(18)])]
    # 200 points (Plan 09e); the report names 100 of them, within the 26 KB report limit
    tmpl = dict(list({n['name']: n['name'] for d in devs for n in d['nodes'] if 'name' in n}.items())[:100])
    # MQTT on too (Plan 09), its broker away: every task the gateway can run, but LTE's.
    import mqtt_check as mc
    mc.post('mqtt', mc.mqtt_cfg(host='192.0.2.1'))
    check(setup(devs, report={'reporting': 1, 'interval': 5, 'template': json.dumps(tmpl)}),
          'set up the largest model: 200 points, both buses, 5 TCP devices; MQTT on')
    slave, tcp = start_all(secs=45)
    time.sleep(40)
    slave.close()
    bc.wait_peer(tcp, 1)
    _, st = w.get_json('/api/status')
    sm = st['mem']['stackMin'] if st else {}
    check(sm.get('words', 0) > 0 and sm.get('task'), '/api/status names the task with the least stack left (%s)' % sm)
    mc.post('mqtt', mc.mqtt_cfg(enabled=0, host=''))
    log = log_text()
    m = re.search(r'edge: stack headroom: (.*) bytes', log)
    hp = re.search(r'edge: heap free (\d+), lowest (\d+)', log)
    boot = re.search(r'edge: memory: .*', log)
    print('     %s' % (boot.group(0) if boot else '-'))
    print('     %s' % (m.group(0) if m else '-'))
    print('     %s' % (hp.group(0) if hp else '-'))
    rooms = [int(x) for x in re.findall(r'edge_\w+ (\d+)', m.group(1))] if m else []
    check(rooms and min(rooms) >= 1024, 'every Edge task keeps at least 1 KB of stack (%s)' % rooms)
    check(hp and int(hp.group(2)) > 60000, 'the heap keeps more than 60 KB at its lowest (%s)' % (hp.group(2) if hp else '-'))


def case_di():
    """The board's DO1 wired to DI1: Edge reads DI1 as the output moves, an on-change report follows."""
    _, ioc = w.get_json('/api/config/io')
    deb = int(ioc.get('diDebounce', 10))
    devs = [io(nodes=[{'ioPoint': 'DO1'}, {'ioPoint': 'DI1', 'onChange': True}])]
    check(setup(devs, report={'reporting': 1, 'periodic': False, 'template': json.dumps({'di1': 'DI1', 'do1': 'DO1'})}),
          'set up: DI1 (wired to DO1) with on-change reports, diDebounce %d ms' % deb)
    set_do(0, False)
    st = Stream()
    st.start()
    time.sleep(2)
    seen = []
    for on in (True, False, True, False):
        n0 = len(reports(st))
        set_do(0, on)
        t0 = time.time()
        got = wait(lambda: [r for r in reports(st)[n0:] if r.get('di1') == int(on)], 3, 0.02)
        seen.append((on, round(time.time() - t0, 2) if got else None))
    st.close()
    check(all(dt is not None and dt < deb / 1000 + 0.5 for _, dt in seen), 'DI1 follows DO1 in an on-change report, within debounce + 0.5 s (%s)' % seen)


def case_onedevice():
    """Plan 09d (Codex code review): all 200 points on one device, Modbus RTU, then Modbus TCP: every point is
    read, and the device's poll task keeps at least 1 KB of stack (its reads are planned at the start, not on it)."""
    pts = [{'name': 'p%d' % i, 'reg': 2 * i, 'timeout': 300} for i in range(200)]  # gaps of 1: 4 reads of up to 125
    # merge on both (plc's default; tcpdev's is off): 4 blocks, one a second, instead of 200 single reads
    for dev, task in ((plc(nodes=pts), 'edge_bus1'), (tcpdev(nodes=pts, merge=True), 'edge_tcp0')):
        check(setup([dev], report={'reporting': 0}), 'set up one %s device with 200 points' % ('RTU' if task == 'edge_bus1' else 'Modbus TCP'))
        slave, tcp = start_all(secs=45)
        full = wait(lambda: status() == 'RUNNING 200/200', 25, 0.5)
        state = status()
        # the headroom is logged once, 30 s after Edge starts: waited for, not timed (the log reads fast since Plan 11b)
        m = wait(lambda: re.search(r'edge: stack headroom: (.*) bytes', log_text()), 45, 1)
        slave.close()
        bc.wait_peer(tcp, 1)
        rooms = dict(re.findall(r'(edge_\w+) (\d+)', m.group(1))) if m else {}
        check(full, '%s: every point read (%s)' % (task, state))
        check(int(rooms.get(task, 0)) >= 1024, '%s keeps at least 1 KB of stack (%s bytes)' % (task, rooms.get(task)))


CASES = {'basic': case_basic, 'change': case_change, 'schedule': case_schedule, 'remote': case_remote, 'split': case_split,
         'gwrtu': case_gwrtu, 'contention': case_contention, 'dead': case_dead, 'late': case_late, 'shortreport': case_shortreport,
         'partial': case_partial, 'bigreport': case_bigreport, 'memory': case_memory, 'di': case_di, 'mqtt': case_mqtt, 'resync': case_resync,
         'onedevice': case_onedevice}


def main():
    if not w.wait_up(5):
        print('the board does not answer on %s' % GW)
        return 1
    for name in sys.argv[1:] or CASES:
        CASES[name]()
    # back to Edge off and the bridges' defaults
    w.req('POST', '/api/config/edge', body=b'{"enabled":false}')
    print('bench: %s' % ('ok' if not bc.failures else 'FAILED (%d)' % len(bc.failures)))
    return 1 if bc.failures else 0


if __name__ == '__main__':
    sys.exit(main())
