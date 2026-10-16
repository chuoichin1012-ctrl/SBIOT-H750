#!/usr/bin/env python3
"""Plan 09 bench check: MQTT (spec §8), the ESP32's test_mqtt.py plus spec §8's cases, on mosquitto in WSL.

The board reaches only Windows, so win_peer.py mqttrelay listens there (1883; 8883 raw, for TLS) and forwards to
mosquitto in WSL through localhost forwarding; it can cut or stall a connection at a given packet. This script starts
and stops the brokers itself. UART1 is the port under test, UART2 the device side (E/F, the RS485 loop).

    python3 stm32/firmware/test/bench/mqtt_check.py [case ...]

Cases: connect publish subscribe ioc will custom qos retained oversized silent flood tls. The NO CA CERTIFICATE and NO CLIENT
CERTIFICATE checks need a board without an mqtt CA / client certificate (bench console "Z" then "WIPE"); otherwise they
are skipped. Ends with MQTT off. Prints one line
per check and "bench: ok".
"""
import datetime
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_check as bc  # noqa: E402  (peer, wait_peer, send_serial, last_hex, adc_pace, check, crc16)
import tls_check as tc  # noqa: E402  (PKI, make_pki, pem, upload, log_text)
import web_check as w  # noqa: E402

WIN = bc.WIN
RUN = '/tmp/sbiot-mqtt'  # the brokers' files, in WSL
PLAIN, AUTHP, TLSP, BIGP = 18830, 18831, 18832, 18840
RELAY, TLS_RELAY = 1883, 8883
check = bc.check


# ---- the brokers and the clients, in WSL ------------------------------------------------------------------

class Broker:
    """mosquitto with a config of its own; stop() ends it."""

    def __init__(self, name, conf, port):
        self.name, self.conf, self.port, self.proc = name, conf, port, None

    def start(self):
        os.makedirs(RUN, exist_ok=True)
        path = os.path.join(RUN, self.name + '.conf')
        with open(path, 'w') as f:
            f.write(self.conf + 'log_dest file %s/%s.log\nlog_type error\nlog_type warning\nlog_type notice\n'
                    'log_type information\n' % (RUN, self.name))
        open(os.path.join(RUN, self.name + '.log'), 'w').close()
        self.proc = subprocess.Popen(['mosquitto', '-c', path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                socket.create_connection(('127.0.0.1', self.port), 0.2).close()
                return self
            except OSError:
                time.sleep(0.1)
        raise RuntimeError('mosquitto %s did not start' % self.name)

    def log(self):
        with open(os.path.join(RUN, self.name + '.log'), errors='replace') as f:
            return f.read()

    def stop(self):
        if self.proc:
            self.proc.terminate()
            self.proc.wait(5)
            self.proc = None


def main_broker():
    """Plain (anonymous), password (gw / s3cret) and TLS (the bench's server certificate for 192.168.0.10)."""
    os.makedirs(RUN, exist_ok=True)
    pw = os.path.join(RUN, 'pw')
    subprocess.run(['mosquitto_passwd', '-b', '-c', pw, 'gw', 's3cret'], check=True, capture_output=True)
    os.chmod(pw, 0o600)
    for n in ('server.pem', 'server.key'):
        shutil.copy(os.path.join(tc.PKI, n), os.path.join(RUN, n))
    conf = ('per_listener_settings true\npersistence false\n'
            'listener %d 0.0.0.0\nallow_anonymous true\n'
            'listener %d 0.0.0.0\nallow_anonymous false\npassword_file %s\n'
            'listener %d 0.0.0.0\nallow_anonymous true\ncertfile %s/server.pem\nkeyfile %s/server.key\n'
            % (PLAIN, AUTHP, pw, TLSP, RUN, RUN))
    return Broker('main', conf, PLAIN)


def big_broker():
    """One message in flight per client: a QoS 1/2 message the board never acknowledges blocks the next one."""
    return Broker('big', 'listener %d 0.0.0.0\nallow_anonymous true\nmax_inflight_messages 1\n' % BIGP, BIGP)


class Sub:
    """mosquitto_sub on a topic filter: (topic, payload bytes) as they arrive."""

    def __init__(self, topic, qos=2, port=PLAIN):
        self.path = os.path.join(RUN, 'sub_%d_%s.txt' % (port, re.sub(r'\W', '_', topic)))
        self.f = open(self.path, 'w')
        self.p = subprocess.Popen(['mosquitto_sub', '-p', str(port), '-t', topic, '-q', str(qos), '-F', '%t|%x'],
                                  stdout=self.f, stderr=subprocess.DEVNULL)
        time.sleep(0.5)

    def msgs(self):
        out = []
        with open(self.path) as f:
            for line in f:
                t, _, h = line.rstrip('\n').partition('|')
                out.append((t, bytes.fromhex(h)))
        return out

    def payloads(self, topic=None):
        return [p for t, p in self.msgs() if topic is None or t == topic]

    def wait(self, payload, secs=10, topic=None):
        end = time.time() + secs
        while time.time() < end:
            if payload in self.payloads(topic):
                return True
            time.sleep(0.2)
        return payload in self.payloads(topic)

    def stop(self):
        self.p.terminate()
        self.p.wait(5)
        self.f.close()


def pub(topic, payload, qos=0, retain=False, port=PLAIN):
    args = ['mosquitto_pub', '-p', str(port), '-t', topic, '-q', str(qos)] + (['-r'] if retain else [])
    if payload:
        subprocess.run(args + ['-s'], input=payload, check=True)
    else:
        subprocess.run(args + ['-n'], check=True)


# ---- the board ----------------------------------------------------------------------------------------

def mqtt_cfg(**kw):
    d = dict(enabled=1, version=1, host=WIN, port=RELAY, clientId='', keepAlive=30, reconnectIfSilent=0, retryInterval=3,
             cleanSession=True, auth=False, username='', password='', will=False, willTopic='', willMessage='', willQos=0,
             willRetain=False, tls=0, tlsVerify=0, sni=False, sniHost='', customMode=0, customModePorts=1)
    d.update(kw)
    return d


def topic(t, mode=0, ports=1, io=False, ident='', delim=',', qos=0, retain=False):
    return dict(enabled=True, mode=mode, topic=t, ports=ports, qos=qos, retain=retain, ioControl=io, identifier=ident,
                delimiter=delim)


def post(name, body):
    s, _, d = w.req('POST', '/api/config/' + name, body=json.dumps(body).encode())
    if s != 200:
        print('     %s: %d %s' % (name, s, d[:120]))
    return s == 200


def configure(mq, pubs=(), subs=(), sys_extra=None, io_extra=None, port1=None):
    """MQTT and its topics, port1 a plain TCP server (no dialling), Edge off, then a restart."""
    ok = post('mqtt', mq) and post('mqttPublish', list(pubs)) and post('mqttSubscribe', list(subs))
    _, p = w.get_json('/api/config/port1')
    p.update(bc.base(serialHeartbeat=False))
    p.update(port1 or {})
    _, sysc = w.get_json('/api/config/system')
    sysc.update({'bufferOffline': 0, 'idleRestart': 0, 'ntp': 0})
    sysc.update(sys_extra or {})
    ok = ok and post('port1', p) and post('system', sysc) and post('edge', {'enabled': False})
    if io_extra:
        _, io = w.get_json('/api/config/io')
        io.update(io_extra)
        ok = ok and post('io', io)
    ok = ok and w.restart()
    time.sleep(2)
    return ok


def state():
    _, st = w.get_json('/api/status')
    return st['mqtt']['state'] if st else ''


def wait_state(want, secs):
    end = time.time() + secs
    while time.time() < end:
        if state() == want:
            return True
        time.sleep(0.5)
    return state() == want


def log_text():
    return tc.log_text()


def connects():
    return len(re.findall(r'mqtt: connected to', log_text()))


def wait_connects(n, secs):
    end = time.time() + secs
    while time.time() < end and connects() < n:
        time.sleep(0.5)
    return connects() >= n


def relay(secs, to=PLAIN, port=RELAY, **kw):
    return bc.peer('mqttrelay', port=port, to=to, secs=secs, **kw)


def evs(ev, kind):
    return [e for e in ev if e.get('ev') == kind]


def from_device(data):
    """UART2 -> UART1: a frame on the port under test."""
    bc.send_serial(data, port=2)


def at_device():
    """(frames, last frame) UART2 got from UART1 since the last call."""
    n, h = bc.last_hex(2)
    return n, bytes.fromhex(h)


# ---- cases --------------------------------------------------------------------------------------------

def case_connect(brk):
    _, st = w.get_json('/api/status')
    mac = st['system']['mac'].replace(':', '').upper() if st else ''
    r = relay(40)
    check(configure(mqtt_cfg()), 'set up: MQTT 3.1.1, no client ID')
    check(wait_state('CONNECTED', 15), 'MQTT 3.1.1: CONNECTED (%s)' % state())
    m = re.findall(r'New client connected from \S+ as (\S+) \(p(\d)', brk.log())
    check(m and m[-1] == ('SBIOT-' + mac[-6:], '2'), 'the derived client ID SBIOT-<MAC>, protocol 3.1.1 (%s)' % m[-1:])
    check(configure(mqtt_cfg(version=0, clientId='gw31')), 'set up: MQTT 3.1')
    check(wait_state('CONNECTED', 15), 'MQTT 3.1: CONNECTED (%s)' % state())
    m = re.findall(r'New client connected from \S+ as (\S+) \(p(\d)', brk.log())
    check(m and m[-1] == ('gw31', '1'), 'the broker saw MQIsdp level 3 and the given ID (%s)' % m[-1:])
    bc.wait_peer(r, 1)
    r = relay(40, to=AUTHP)
    check(configure(mqtt_cfg(auth=True, username='gw', password='wrong')), 'set up: a wrong password')
    check(wait_state('REFUSED', 15), 'wrong password: REFUSED (%s)' % state())
    check(configure(mqtt_cfg(auth=True, username='gw', password='s3cret')), 'set up: the right password')
    check(wait_state('CONNECTED', 15), 'right password: CONNECTED (%s)' % state())
    bc.wait_peer(r, 1)


def case_publish(brk):
    r = relay(40)
    check(configure(mqtt_cfg(), pubs=[topic('gw/up'), topic('gw/dist', mode=1, ident='#A'), topic('gw/both', ports=3)]),
          'set up: transparent on UART1, distribute "#A", a topic on both ports')
    check(wait_state('CONNECTED', 15), 'CONNECTED')
    s = Sub('gw/#')
    from_device(b'hello')
    check(s.wait(b'hello', topic='gw/up') and s.wait(b'hello', topic='gw/both'), 'transparent publish (%s)' % s.msgs()[-3:])
    from_device(b'#Axyz')  # no comma after the identifier: not for gw/dist
    from_device(b'#A,xyz')
    check(s.wait(b'xyz', topic='gw/dist'), 'distribute: "identifier,payload" published as the payload')
    check(s.payloads('gw/dist') == [b'xyz'], '... only when the identifier is a whole field (%s)' % s.payloads('gw/dist'))
    bc.send_serial(b'two', port=1)  # UART1 -> UART2: the port under test is UART2 now
    check(s.wait(b'two', topic='gw/both'), 'a topic bound to both ports takes UART2 too')
    check(b'two' not in s.payloads('gw/up'), "... and UART1's topic does not")
    s.stop()
    bc.wait_peer(r, 1)


def case_subscribe(brk):
    r = relay(40)
    subs = [topic('gw/down'), topic('gw/cmd/+', mode=1, delim='|'), topic('gw/all/#')]
    check(configure(mqtt_cfg(), subs=subs), 'set up: payload only, topic|payload on gw/cmd/+, gw/all/#')
    check(wait_state('CONNECTED', 15), 'CONNECTED')
    at_device()
    for t, payload, want, what in (('gw/down', b'd1', b'd1', 'payload only'),
                                   ('gw/cmd/7', b'on', b'gw/cmd/7|on', 'topic + delimiter + payload, "+"'),
                                   ('gw/all/a/b', b'deep', b'deep', '"#" over two levels')):
        pub(t, payload, qos=1)
        time.sleep(1)
        n, last = at_device()
        check(n == 1 and last == want, 'subscribe: %s (%d frame(s), %r)' % (what, n, last))
    pub('gw/cmd/7/x', b'no', qos=1)
    time.sleep(1)
    n, last = at_device()
    check(n == 0, '"+" matches one level only (%d frame(s) %r)' % (n, last))
    bc.wait_peer(r, 1)


def rtu(body):
    return body + struct.pack('<H', bc.crc16(body))


def case_ioc(brk):
    r = relay(40)
    check(configure(mqtt_cfg(), pubs=[topic('gw/ioreply', ports=0, io=True)], subs=[topic('gw/io', io=True)],
                    io_extra={'modbusAddress': 200}), 'set up: IO control on gw/io (bound to UART1), replies on gw/ioreply')
    check(wait_state('CONNECTED', 15), 'CONNECTED')
    for i in (0, 1):
        w.req('POST', '/api/io', body=json.dumps({'do': i, 'value': False}).encode())
    s = Sub('gw/ioreply')
    at_device()
    write, read = rtu(bytes([200, 5, 0, 1, 0xFF, 0])), rtu(bytes([200, 1, 0, 0, 0, 2]))
    pub('gw/io', write, qos=1)
    time.sleep(1)
    pub('gw/io', read, qos=1)
    check(s.wait(write) and s.wait(rtu(bytes([200, 1, 1, 2]))), 'IO control: the coil write echoed, a read shows DO2 on (%s)'
          % [p.hex() for p in s.payloads()])
    _, d = w.get_json('/api/io')
    check(d and d['do'][1] is True, 'DO2 really on')
    n, _ = at_device()
    check(n == 0, 'IO frames are not forwarded to the port (%d)' % n)
    pub('gw/io', b'plain', qos=1)
    time.sleep(1)
    n, last = at_device()
    check(n == 1 and last == b'plain', 'other payloads on an ioControl topic go on to the port (%r)' % last)
    w.req('POST', '/api/io', body=json.dumps({'do': 1, 'value': False}).encode())
    s.stop()
    bc.wait_peer(r, 1)


def case_will(brk):
    r = relay(30, cut='up:PINGREQ:1')
    s = Sub('gw/will')
    check(configure(mqtt_cfg(keepAlive=5, will=True, willTopic='gw/will', willMessage='bye', willQos=1)),
          'set up: last will gw/will "bye", keep-alive 5 s')
    check(wait_state('CONNECTED', 15), 'CONNECTED')
    check(s.wait(b'bye', 20), 'the last will is published when the gateway drops off (the relay cuts at the first PINGREQ)')
    ev = bc.wait_peer(r, 1)
    check(evs(ev, 'cut') and len(evs(ev, 'accept')) >= 2, 'the gateway connected again after the cut')
    s.stop()


def case_custom(brk):
    # The broker away (the relay closes every connection at once), bufferOffline on, a frame about every second:
    # the retries keep to retryInterval whatever is queued.
    r = relay(16, refuse='1')
    check(configure(mqtt_cfg(customMode=1, customModePorts=1, retryInterval=3), sys_extra={'bufferOffline': 1}),
          'set up: custom mode on UART1, bufferOffline, the broker away')
    from_device(b'gw/c,0,1,early')  # QoS 0, retained: the subscriber may come after the gateway connects
    for i in range(6):
        from_device(b'gw/c,1,0,q%d' % i)
    ev = bc.wait_peer(r, 1)
    acc = [e['t'] for e in evs(ev, 'accept')]
    gaps = [b - a for a, b in zip(acc, acc[1:])]
    check(len(gaps) >= 2 and all(g >= 2800 for g in gaps), 'offline: a connect every retryInterval, not per queued frame (%s ms)' % gaps)
    s = Sub('gw/c', qos=1)
    r = relay(30)
    check(s.wait(b'early', 20), 'custom mode + bufferOffline: queued while offline (QoS 0 too), sent after connecting')
    check(all(s.wait(b'q%d' % i, 5) for i in range(6)), 'the QoS 1 frames too (%s)' % s.payloads())
    from_device(b'gw/c,0,0,a,b,c')
    check(s.wait(b'a,b,c'), 'custom mode: a payload with commas')
    from_device(b'not custom')
    check(re.search(r'custom mode: UART1 frame is not', log_text()), 'a frame not in the custom format is logged, not sent')
    s.stop()
    pub('gw/c', b'', retain=True)
    bc.wait_peer(r, 1)


def case_qos(brk):
    """2 versions x cleanSession 0/1 x a cut at each QoS phase: kept sessions lose nothing and do not duplicate QoS 2;
    new sessions lose nothing."""
    for version in (1, 0):
        for clean in (False, True):
            name = 'MQTT %s, cleanSession %d' % ('3.1.1' if version else '3.1', clean)
            # bufferOffline: a frame that comes while the gateway connects again waits in the outbox (without it,
            # spec §8 drops it). The relay starts after the restart, so its first connection is this config's.
            check(configure(mqtt_cfg(version=version, cleanSession=clean, clientId='gwq'),
                            pubs=[topic('q/1', qos=1), topic('q/2', qos=2)], sys_extra={'bufferOffline': 1}),
                  'set up: %s, QoS 1 and 2 topics, bufferOffline' % name)
            for cut in ('down:PUBACK:1', 'down:PUBREC:1', 'down:PUBCOMP:1'):
                n0 = connects()
                r = relay(30, cut=cut)
                check(wait_connects(n0 + 1, 15), '%s: connected through the relay' % name)
                s = Sub('q/#')
                tag = 'v%dc%d%s' % (version, clean, cut.split(':')[1][3:].lower())
                msgs = [('%s-%d' % (tag, i)).encode() for i in range(4)]
                for m in msgs:
                    from_device(m)
                ev = bc.wait_peer(r, 1)
                time.sleep(1)
                q1 = s.payloads('q/1')
                q2 = s.payloads('q/2')
                s.stop()
                lost = [m for m in msgs if m not in q1 or m not in q2]
                dup2 = [m for m in msgs if q2.count(m) > 1]
                kept = re.findall(r'connected to \S+ \(MQTT [\d.]+, (kept|new) session\)', log_text())
                check(evs(ev, 'cut'), '%s, cut at %s: the relay cut' % (name, cut))
                if clean:
                    check(not lost, '%s, cut at %s: new session, nothing lost (lost %s)' % (name, cut, lost))
                else:
                    check(not lost and not dup2 and kept and kept[-1] == 'kept',
                          '%s, cut at %s: kept session, nothing lost, no QoS 2 duplicate (lost %s, dup %s, %s)'
                          % (name, cut, lost, dup2, kept[-1:]))


def case_retained(brk):
    pub('gw/ret', b'keep', qos=1, retain=True)
    r = relay(14)
    check(configure(mqtt_cfg(), subs=[topic('gw/ret', qos=1)]), 'set up: a subscription with a retained message')
    check(wait_state('CONNECTED', 15), 'CONNECTED')
    time.sleep(1)
    n, last = at_device()
    check(n >= 1 and last == b'keep', 'the retained message reaches the port (%d, %r)' % (n, last))
    bc.wait_peer(r, 1)  # the relay ends: the connection drops
    n0 = connects()
    r = relay(20)
    check(wait_connects(n0 + 1, 15), 'connected again')
    time.sleep(1)
    n, last = at_device()
    check(n == 1 and last == b'keep', 'after a reconnect the retained message reaches the port again (%d, %r)' % (n, last))
    pub('gw/ret', b'', retain=True)
    bc.wait_peer(r, 1)


def case_oversized(brk):
    big = big_broker().start()
    try:
        r = relay(40, to=BIGP)
        check(configure(mqtt_cfg(), subs=[topic('gw/big', qos=2)]), 'set up: a broker with one message in flight, gw/big at QoS 2')
        check(wait_state('CONNECTED', 15), 'CONNECTED')
        n0 = connects()
        at_device()
        payload = bytes(range(256)) * 24  # 6 KB, over the 4 KB receive buffer
        for qos in (0, 1, 2):
            pub('gw/big', payload, qos=qos, port=BIGP)
        pub('gw/big', b'after', qos=1, port=BIGP)
        time.sleep(3)
        n, last = at_device()
        check(n == 1 and last == b'after', 'a 6 KB message at QoS 0, 1 and 2 is dropped, the next QoS 1 one arrives (%d, %r)' % (n, last))
        pub('gw/big', payload, qos=1, retain=True, port=BIGP)
        pub('gw/big', b'after2', qos=1, port=BIGP)
        time.sleep(3)
        n, last = at_device()
        check(n == 1 and last == b'after2', 'a retained 6 KB one too (%d, %r)' % (n, last))
        drops = len(re.findall(r'larger than the buffer was dropped', log_text()))
        check(drops == 3, 'each QoS 1/2 drop logged, its acknowledgement sent (%d)' % drops)
        check(connects() == n0 and state() == 'CONNECTED', 'the connection stayed up')
        pub('gw/big', b'', retain=True, port=BIGP)
        bc.wait_peer(r, 1)
    finally:
        big.stop()


def case_silent(brk):
    r = relay(45)
    check(configure(mqtt_cfg(keepAlive=5, reconnectIfSilent=20)), 'set up: reconnectIfSilent 20 s, keep-alive 5 s')
    check(wait_state('CONNECTED', 15), 'CONNECTED')
    time.sleep(30)
    t = log_text()
    up = [int(x) for x in re.findall(r'I \((\d+)\) mqtt: connected to', t)]
    gone = [int(x) for x in re.findall(r'W \((\d+)\) mqtt: nothing received for 20 s', t)]
    ev = bc.wait_peer(r, 1)
    pings = [e for e in evs(ev, 'pkt') if e['type'] == 'PINGRESP' and e['conn'] == 0]
    check(len(up) >= 2 and gone and 19500 <= gone[0] - up[0] <= 21500,
          'no PUBLISH for 20 s: reconnects (after %s ms), and connects again' % [g - up[0] for g in gone[:1]])
    check(len(pings) >= 2, 'PINGRESPs came meanwhile (%d): they do not count' % len(pings))
    # The relay stops in the middle of a message to the board: IO keeps its pace while coreMQTT waits.
    r = relay(50, stall='down:PUBLISH:1:5')
    check(configure(mqtt_cfg(keepAlive=10), subs=[topic('gw/s')]), 'set up: keep-alive 10 s, the relay stalls the first message')
    check(wait_state('CONNECTED', 15), 'CONNECTED')
    pub('gw/s', b'x' * 3000, qos=1)
    a0 = bc.adc_pace()
    time.sleep(15)
    a1 = bc.adc_pace()
    wins = (a1[1] - a0[1]) / (a1[0] - a0[0]) if a0 and a1 else -1
    late = a1[2] - a0[2] if a0 and a1 else -1
    check(9.5 <= wins <= 10.5 and late == 0, 'a message stalled after 5 bytes: ADC %.2f windows/s, late +%d' % (wins, late))
    ev = bc.wait_peer(r, 1)
    check(evs(ev, 'stall') and re.search(r'mqtt: connection lost', log_text()) and len(evs(ev, 'accept')) >= 2,
          'no PINGRESP gets through: the connection is dropped and made again')


def case_flood(brk):
    """The broker sends without a pause: tasks below MQTT's priority keep running (Codex code review). UART1's serial
    heartbeat (`serial_hb`, priority 4) every second is counted on UART2."""
    r = relay(60)
    hb = {'serialHeartbeat': True, 'serialHeartbeatContent': 0, 'serialHeartbeatHex': 1, 'serialHeartbeatData': '4842',
          'serialHeartbeatInterval': 1}
    check(configure(mqtt_cfg(), subs=[topic('gw/flood', ports=0)], port1=hb), 'set up: gw/flood (no port), a heartbeat on UART1 every 1 s')
    check(wait_state('CONNECTED', 15), 'CONNECTED')
    at_device()
    t0 = time.time()
    n, _ = at_device()
    quiet = n / (time.time() - t0)
    lines = b''.join(b'%06d\n' % i for i in range(200000))
    flood = subprocess.Popen(['mosquitto_pub', '-p', str(PLAIN), '-t', 'gw/flood', '-q', '0', '-l'], stdin=subprocess.PIPE)
    flood.stdin.write(lines)
    flood.stdin.close()
    time.sleep(2)
    t0 = time.time()
    time.sleep(8)
    n, _ = at_device()
    busy = n / (time.time() - t0)
    flood.wait(120)
    ev = bc.wait_peer(r, 1)
    down = sum(1 for e in evs(ev, 'pkt') if e['type'] == 'PUBLISH' and e['dir'] == 'down')
    check(busy >= 0.7, 'under a flood (%d messages relayed): the heartbeat %.2f/s (quiet %.2f/s)' % (down, busy, quiet))


def case_tls(brk):
    tc.make_pki()
    r = relay(50, to=TLSP, port=TLS_RELAY, raw='1')
    tls = dict(port=TLS_RELAY, tls=2, tlsVerify=1)
    check(configure(mqtt_cfg(**tls), pubs=[topic('gw/up')]), 'set up: TLS, the server verified')
    st = state()
    if st == 'NO CA CERTIFICATE':
        check(True, 'tlsVerify 1 without a CA: NO CA CERTIFICATE, MQTT stays off')
    else:
        print('skip tlsVerify 1 without a CA (the board holds an mqtt CA already: %s)' % st)
    check(configure(mqtt_cfg(port=TLS_RELAY, tls=2, tlsVerify=2), pubs=[topic('gw/up')]), 'set up: TLS, both sides verified')
    st = state()
    if st == 'NO CLIENT CERTIFICATE':
        check(True, 'tlsVerify 2 without a client certificate: NO CLIENT CERTIFICATE, MQTT stays off')
    else:
        print('skip tlsVerify 2 without a client certificate (the board holds one: %s)' % st)
    s, i = tc.upload('mqtt', 'ca', tc.pem('ca.pem'))
    check(s == 200 and i.get('restartNeeded') is True, 'the mqtt CA upload says a restart is needed')
    check(configure(mqtt_cfg(**tls), pubs=[topic('gw/up')]), 'restart with the CA')
    check(wait_state('CONNECTED', 20), 'TLS with the CA (name: host 192.168.0.10): CONNECTED (%s)' % state())
    sub = Sub('gw/up')
    from_device(b'over-tls')
    check(sub.wait(b'over-tls'), 'TLS: a publish arrives')
    sub.stop()
    room = [int(x) for x in re.findall(r'mqtt: stack headroom after the TLS handshake: (\d+)', log_text())]
    _, stj = w.get_json('/api/status')
    heap = (stj['system']['heapFree'], stj['system']['heapMin']) if stj else (-1, -1)
    check(room and room[-1] >= 1024, 'mqtt: %s bytes of stack left after an RSA-2048 handshake; heap free/min %s' % (room[-1:], heap))
    bc.wait_peer(r, 1)
    r = relay(60, to=TLSP, port=TLS_RELAY, raw='1')
    check(configure(mqtt_cfg(port=TLS_RELAY, tls=2, tlsVerify=0)), 'set up: TLS, no certificate check')
    check(wait_state('CONNECTED', 20), 'tlsVerify 0: CONNECTED (%s)' % state())
    check(configure(mqtt_cfg(sni=True, sniHost='wrong.test', **tls)), 'set up: sniHost wrong.test')
    time.sleep(12)
    check(state() != 'CONNECTED' and re.search(r'certificate rejected', log_text()),
          'a certificate for another name is rejected (%s)' % state())
    bc.wait_peer(r, 1)
    # The clock set 5 days ahead: the 2-day server certificate has expired.
    check(configure(mqtt_cfg(**tls)), 'set up: TLS, the server verified (no relay yet)')
    _, sysc = w.get_json('/api/config/system')
    tz = int(sysc.get('timezone', 0)) if sysc else 0
    later = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=5, hours=tz)
    s, _, _ = w.req('POST', '/api/time', body=json.dumps({'time': later.strftime('%Y-%m-%d %H:%M:%S')}).encode())
    r = relay(15, to=TLSP, port=TLS_RELAY, raw='1')
    time.sleep(10)
    check(s == 200 and state() != 'CONNECTED' and re.search(r'certificate rejected \(flags 0x1\)', log_text()),
          'clock set: an expired server certificate is rejected (%s)' % state())
    bc.wait_peer(r, 1)


CASES = {'connect': case_connect, 'publish': case_publish, 'subscribe': case_subscribe, 'ioc': case_ioc,
         'will': case_will, 'custom': case_custom, 'qos': case_qos, 'retained': case_retained,
         'oversized': case_oversized, 'silent': case_silent, 'flood': case_flood, 'tls': case_tls}


def mqtt_off():
    post('mqtt', mqtt_cfg(enabled=0, host=''))
    post('mqttPublish', [])
    post('mqttSubscribe', [])
    _, sysc = w.get_json('/api/config/system')
    sysc.update({'bufferOffline': 0})
    post('system', sysc)
    w.restart()


def main():
    if not w.wait_up(5):
        print('the board does not answer on %s' % bc.GW)
        return 1
    tc.make_pki()
    brk = main_broker().start()
    try:
        for name in sys.argv[1:] or CASES:
            CASES[name](brk)
    finally:
        mqtt_off()
        brk.stop()
    print('bench: %s' % ('ok' if not bc.failures else 'FAILED (%d)' % len(bc.failures)))
    return 1 if bc.failures else 0


if __name__ == '__main__':
    sys.exit(main())
