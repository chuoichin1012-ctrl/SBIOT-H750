"""Bench peers on the Windows side (Plan 08a): the board dials Windows (WSL is behind NAT). Run by
bridge_check.py with Windows' Python; prints one JSON line per event, with ms since the start.

    python win_peer.py MODE key=value ...

MODE tcpserver  port, secs, delay_ms (listen only after), send_hex + send_ms (to each new connection,
                ms after its accept), drop_ms (close the first connection that long after its accept),
                modbus=1 (answer each Modbus TCP read with the registers 0x1234 + i, or + the address with addr=1; echo writes; an event per
                request), late=1 (one reply late_ms, default 1500, after its request), silent=1 (read, never answer)
     udp        port, secs, send_hex + send_ms + to=host:port
     http       port, secs, reply (the body)
     mcast      group, port, iface, send_hex, send_ms
     manyclients host, port, n, secs: n TCP connections held (Plan 11b)
     slowget    host, port, secs: GET / of the UI read at 1 KB/s (Plan 11b)
     tlsset     pm, p1, p2, secs, cert, key[, hold]: three TLS listeners in one process, one clock (Plan 11b)
     disc       iface, port (48600), secs, send (the packet, hex): broadcast from iface, one event per reply (Plan 12)
     stuckpair  host, port, secs: two clients of a TCP server, one never reading, one logging what it got
     tlsserver  port, secs, cert, key (Windows paths), clientca (require a client certificate), send_hex +
                send_ms (after the handshake), big=N (one send of N bytes after the handshake), silent=1 (accept
                TCP, never answer the handshake); events hs (cipher, sni, peer), hsfail, data, close (Plan 08b)
     https      port, secs, cert, key, reply: the http mode over TLS
     mqttrelay  port, secs, to (the broker's port on 127.0.0.1: mosquitto in WSL, through localhost forwarding),
                cut=DIR:TYPE:N (on the first connection, close both sides instead of forwarding the Nth packet of TYPE
                going DIR, up = board to broker, down = broker to board; e.g. up:PUBREL:1), hold_ms (after the cut,
                refuse new connections that long), stall=DIR:TYPE:N:K (on the first connection, forward only the first
                K bytes of that packet, then nothing more that way), refuse=1 (accept, then close at once), raw=1 (bytes
                as they come, no parsing: TLS); events pkt (dir, type, len, conn), cut, stall, accept, refused (Plan 09)
"""
import json
import socket
import ssl
import struct
import sys
import threading
import time

T0 = time.time()
LOCK = threading.Lock()
WRITES = {}  # Modbus TCP registers written with FC6
LATE = []


def out(**k):
    k['t'] = round((time.time() - T0) * 1000)
    with LOCK:
        print(json.dumps(k), flush=True)


def tcp_conn(c, a, args, first):
    send_hex, send_ms = args.get('send_hex'), float(args.get('send_ms', 0))
    drop_ms = float(args.get('drop_ms', 0)) if first else 0
    t_acc = time.time()
    c.settimeout(0.05)
    sent = False
    buf = b''
    while time.time() - T0 < float(args.get('secs', 20)):
        now = (time.time() - t_acc) * 1000
        if send_hex and not sent and now >= send_ms:
            c.sendall(bytes.fromhex(send_hex))
            sent = True
        if drop_ms and now >= drop_ms:
            out(ev='drop', peer=a[0])
            c.close()
            return
        try:
            d = c.recv(4096)
        except socket.timeout:
            continue
        except OSError:
            break
        if not d:
            break
        out(ev='data', hex=d.hex())
        if args.get('modbus') == '1' and args.get('silent') != '1':
            buf += d
            while len(buf) >= 12:
                n = struct.unpack('>H', buf[4:6])[0] + 6
                if len(buf) < n:
                    break
                adu, buf = buf[:n], buf[n:]
                tid, unit, fc = adu[0:2], adu[6], adu[7]
                start, count = struct.unpack('>HH', adu[8:12])
                out(ev='req', fc=fc, start=start, count=count, tid=struct.unpack('>H', tid)[0])
                if fc in (5, 6, 15, 16):  # writes: the echo (unit, fc, address and value or count)
                    pdu = adu[6:12]
                    if fc == 6:
                        WRITES[start] = count
                elif fc in (1, 2):
                    pdu = bytes([unit, fc, (count + 7) // 8]) + bytes((count + 7) // 8)
                else:  # registers 0x1234 + i (with addr=1: + the address), or what a write left there
                    base = start if args.get('addr') == '1' else 0
                    regs = b''.join(struct.pack('>H', WRITES.get(start + i, 0x1234 + base + i)) for i in range(count))
                    pdu = bytes([unit, fc, len(regs)]) + regs
                if args.get('late') == '1' and not LATE:  # once: past the poller's timeout
                    LATE.append(1)
                    time.sleep(float(args.get('late_ms', 1500)) / 1000)
                c.sendall(tid + b'\0\0' + struct.pack('>H', len(pdu)) + pdu)
    out(ev='close')
    c.close()


def tcpserver(args):
    time.sleep(float(args.get('delay_ms', 0)) / 1000)
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('0.0.0.0', int(args['port'])))
    s.listen(4)
    s.settimeout(0.2)
    out(ev='listen')
    first = True
    while time.time() - T0 < float(args.get('secs', 20)):
        try:
            c, a = s.accept()
        except socket.timeout:
            continue
        out(ev='accept', peer=a[0])
        threading.Thread(target=tcp_conn, args=(c, a, args, first), daemon=True).start()
        first = False
    time.sleep(0.2)


def udp(args):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('0.0.0.0', int(args['port'])))
    s.settimeout(0.05)
    sent = False
    while time.time() - T0 < float(args.get('secs', 20)):
        if args.get('send_hex') and not sent and (time.time() - T0) * 1000 >= float(args.get('send_ms', 0)):
            host, port = args['to'].split(':')
            s.sendto(bytes.fromhex(args['send_hex']), (host, int(port)))
            sent = True
        try:
            d, a = s.recvfrom(65535)
        except socket.timeout:
            continue
        out(ev='data', hex=d.hex(), peer=a[0], port=a[1])


def http(args):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('0.0.0.0', int(args['port'])))
    s.listen(4)
    s.settimeout(0.2)
    while time.time() - T0 < float(args.get('secs', 20)):
        try:
            c, a = s.accept()
        except socket.timeout:
            continue
        c.settimeout(3)
        data = b''
        while b'\r\n\r\n' not in data:
            d = c.recv(4096)
            if not d:
                break
            data += d
        head, _, body = data.partition(b'\r\n\r\n')
        lines = head.decode(errors='replace').split('\r\n')
        hdrs = dict(l.split(': ', 1) for l in lines[1:] if ': ' in l)
        n = int(hdrs.get('Content-Length', 0))
        while len(body) < n:
            body += c.recv(4096)
        method, path = lines[0].split(' ')[:2]
        out(ev='request', method=method, path=path, headers=hdrs, body=body.hex())
        c.sendall(b'HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n' + args.get('reply', 'OK').encode())
        c.close()


def tls_ctx(args, sni):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(args['cert'], args['key'])
    if args.get('clientca'):
        ctx.verify_mode = ssl.CERT_REQUIRED
        ctx.load_verify_locations(args['clientca'])
    ctx.sni_callback = lambda sobj, name, c: sni.append(name)
    return ctx


def tls_conn(c, a, args, ctx, sni):
    if args.get('silent') == '1':  # TCP only: the handshake never gets an answer
        c.settimeout(0.2)
        while time.time() - T0 < float(args.get('secs', 20)):
            try:
                if not c.recv(4096):
                    break
            except socket.timeout:
                continue
            except OSError:
                break
        c.close()
        return
    c.settimeout(15)
    try:
        t = ctx.wrap_socket(c, server_side=True)
    except (ssl.SSLError, OSError) as e:
        out(ev='hsfail', err=str(e)[:160])
        c.close()
        return
    peer = t.getpeercert()
    cn = dict(x[0] for x in peer['subject']).get('commonName') if peer else None
    out(ev='hs', cipher=t.cipher()[0], version=t.version(), sni=sni[-1] if sni else None, peer=cn)
    if args.get('big'):
        n = int(args['big'])
        t.sendall(bytes(i % 256 for i in range(n)))
    tcp_conn(t, a, args, False)


def tlsserver(args):
    sni = []
    ctx = tls_ctx(args, sni)
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('0.0.0.0', int(args['port'])))
    s.listen(4)
    s.settimeout(0.2)
    out(ev='listen')
    while time.time() - T0 < float(args.get('secs', 20)):
        try:
            c, a = s.accept()
        except socket.timeout:
            continue
        out(ev='accept', peer=a[0])
        threading.Thread(target=tls_conn, args=(c, a, args, ctx, sni), daemon=True).start()
    time.sleep(0.2)


def https(args):
    sni = []
    ctx = tls_ctx(args, sni)
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('0.0.0.0', int(args['port'])))
    s.listen(4)
    s.settimeout(0.2)
    while time.time() - T0 < float(args.get('secs', 20)):
        try:
            c, a = s.accept()
        except socket.timeout:
            continue
        c.settimeout(10)
        try:
            t = ctx.wrap_socket(c, server_side=True)
        except (ssl.SSLError, OSError) as e:
            out(ev='hsfail', err=str(e)[:160])
            c.close()
            continue
        data = b''
        try:
            while b'\r\n\r\n' not in data:
                d = t.recv(4096)
                if not d:
                    break
                data += d
        except (ssl.SSLError, OSError):
            pass
        line = data.split(b'\r\n', 1)[0].decode(errors='replace').split(' ')
        out(ev='request', method=line[0], path=line[1] if len(line) > 1 else '', sni=sni[-1] if sni else None)
        try:
            t.sendall(b'HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n' + args.get('reply', 'OK').encode())
            t.close()
        except (ssl.SSLError, OSError):
            pass


MQTT_TYPES = ['', 'CONNECT', 'CONNACK', 'PUBLISH', 'PUBACK', 'PUBREC', 'PUBREL', 'PUBCOMP', 'SUBSCRIBE', 'SUBACK',
              'UNSUBSCRIBE', 'UNSUBACK', 'PINGREQ', 'PINGRESP', 'DISCONNECT', 'AUTH']


def mqtt_split(buf):
    """Whole MQTT packets at the front of buf: ([packet], rest)."""
    pkts = []
    while len(buf) >= 2:
        n, mul, i = 0, 1, 1
        while i < len(buf) and i <= 4:
            n += (buf[i] & 0x7F) * mul
            mul *= 128
            i += 1
            if not buf[i - 1] & 0x80:
                break
        else:
            break
        if buf[i - 1] & 0x80 or len(buf) < i + n:
            break
        pkts.append(buf[:i + n])
        buf = buf[i + n:]
    return pkts, buf


def mqtt_pipe(src, dst, way, conn, state):
    buf, counts, stalled = b'', {}, False
    while not state['closed']:
        try:
            d = src.recv(65536)
        except socket.timeout:
            continue
        except OSError:
            break
        if not d:
            break
        if stalled:
            continue  # read and dropped: the board sees a packet that never ends
        if state['raw']:
            try:
                dst.sendall(d)
            except OSError:
                break
            continue
        pkts, buf = mqtt_split(buf + d)
        for p in pkts:
            kind = MQTT_TYPES[p[0] >> 4]
            counts[kind] = counts.get(kind, 0) + 1
            info = {'dir': way, 'type': kind, 'len': len(p), 'conn': conn}
            out(ev='pkt', **info, hex=p[:48].hex())
            if conn == 0 and state['cut'] == (way, kind, counts[kind]):
                out(ev='cut', **info)
                state['closed'] = True
                state['cut_at'] = time.time()
                break
            if conn == 0 and state['stall'] and state['stall'][:3] == (way, kind, counts[kind]):
                out(ev='stall', **info)
                try:
                    dst.sendall(p[:state['stall'][3]])
                except OSError:
                    pass
                stalled = True
                break
            try:
                dst.sendall(p)
            except OSError:
                state['closed'] = True
                break
    state['closed'] = True
    for x in (src, dst):
        try:
            x.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        x.close()


def mqttrelay(args):
    cut = args.get('cut')
    cut = (cut.split(':')[0], cut.split(':')[1], int(cut.split(':')[2])) if cut else None
    stall = args.get('stall')
    stall = tuple(int(x) if i >= 2 else x for i, x in enumerate(stall.split(':'))) if stall else None
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('0.0.0.0', int(args['port'])))
    s.listen(4)
    s.settimeout(0.2)
    out(ev='listen')
    conn = 0
    states = []
    while time.time() - T0 < float(args.get('secs', 20)):
        try:
            c, a = s.accept()
        except socket.timeout:
            continue
        hold = float(args.get('hold_ms', 0)) / 1000
        if any(st.get('cut_at') and time.time() - st['cut_at'] < hold for st in states):
            out(ev='refused', peer=a[0])
            c.close()
            continue
        if args.get('refuse') == '1':
            out(ev='accept', peer=a[0], conn=conn)
            conn += 1
            c.close()
            continue
        try:
            b = socket.create_connection(('127.0.0.1', int(args['to'])), 3)
        except OSError as e:
            out(ev='nobroker', err=str(e))
            c.close()
            continue
        for x in (c, b):
            x.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            x.settimeout(0.2)
        st = {'closed': False, 'cut': cut, 'stall': stall, 'raw': args.get('raw') == '1'}
        states.append(st)
        out(ev='accept', peer=a[0], conn=conn)
        threading.Thread(target=mqtt_pipe, args=(c, b, 'up', conn, st), daemon=True).start()
        threading.Thread(target=mqtt_pipe, args=(b, c, 'down', conn, st), daemon=True).start()
        conn += 1
    time.sleep(0.2)


def mcast(args):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(args['iface']))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    time.sleep(float(args.get('send_ms', 0)) / 1000)
    s.sendto(bytes.fromhex(args['send_hex']), (args['group'], int(args['port'])))
    out(ev='sent')


def disc(args):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind((args['iface'], 0))  # the LAN adapter: the broadcast leaves by it, the replies come back to it
    s.settimeout(0.1)
    s.sendto(bytes.fromhex(args['send']), ('255.255.255.255', int(args.get('port', 48600))))
    while time.time() - T0 < float(args.get('secs', 3)):
        try:
            d, a = s.recvfrom(2048)
        except socket.timeout:
            continue
        out(ev='reply', text=d.decode('utf-8', 'replace'), peer=a[0])


def stuckpair(args):
    """Two clients of the board's TCP server: one never reads (a tiny window), one reads and logs."""
    stuck = socket.socket()
    stuck.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    stuck.connect((args['host'], int(args['port'])))
    ok = socket.create_connection((args['host'], int(args['port'])), 5)
    out(ev='connected')
    ok.settimeout(0.5)
    n = 0
    while time.time() - T0 < float(args.get('secs', 30)):
        try:
            d = ok.recv(65536)
        except socket.timeout:
            continue
        if not d:
            break
        n += len(d)
        out(ev='got', n=n)
    stuck.close()
    ok.close()



def manyclients(args):
    """host, port, n, secs: n TCP connections to host:port, held (Plan 11b, scenario A of spec §12.3)."""
    socks = []
    for i in range(int(args['n'])):
        try:
            socks.append(socket.create_connection((args['host'], int(args['port'])), 5))
        except OSError as e:
            out(ev='refused', i=i, err=str(e))
    out(ev='connected', n=len(socks), port=int(args['port']))
    while time.time() - T0 < float(args.get('secs', 60)):
        time.sleep(0.5)
    for c in socks:
        c.close()


def slowget(args):
    """host, port, secs: GET / of the UI, read 1 KB a second (Plan 11b, scenario B)."""
    c = socket.create_connection((args['host'], int(args.get('port', 80))), 10)
    c.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
    c.sendall(b'GET / HTTP/1.1\r\nHost: gw\r\nAccept-Encoding: gzip\r\n\r\n')
    c.settimeout(5)
    got = 0
    while time.time() - T0 < float(args.get('secs', 60)):
        try:
            d = c.recv(1024)
        except socket.timeout:
            continue
        if not d:
            break
        got += len(d)
        time.sleep(1)
    out(ev='slow', got=got)
    c.close()


def tlsset(args):
    """pm, p1, p2, secs, cert, key: three TLS listeners in one process, so their times share one clock (Plan 11b,
    scenario B; Codex): pm (MQTT) completes the handshake and never answers (no CONNACK), p1 (UART1's TLS client)
    holds the session, p2 answers HTTPS "OK". Events accept/hs/hsfail with the port and t. The handshakes wait until
    each port has a connection (at most `hold` s; the gateway's handshake timeout is 10 s), then run at once: so the
    gateway's three handshakes are in flight together, whatever its retry times."""
    ctx = tls_ctx(args, [])
    secs = float(args.get('secs', 60))
    hold = float(args.get('hold', 6))
    seen = set()
    all_in = threading.Event()

    def conn(c, port, kind):
        try:
            with LOCK:
                seen.add(port)
                if len(seen) == 3:
                    all_in.set()
            all_in.wait(hold)
            t = ctx.wrap_socket(c, server_side=True)
            out(ev='hs', port=port)
            t.settimeout(1)
            while time.time() - T0 < secs:
                try:
                    d = t.recv(4096)
                except socket.timeout:
                    continue
                if not d:
                    break
                if kind == 'https' and b'\r\n\r\n' in d:
                    t.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK')
                    break
        except (OSError, ssl.SSLError) as e:
            out(ev='hsfail', port=port, err=str(e)[:80])
        finally:
            c.close()

    def serve(port, kind):
        ls = socket.socket()
        ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        ls.bind(('0.0.0.0', port))
        ls.listen(8)
        ls.settimeout(0.2)
        while time.time() - T0 < secs:
            try:
                c, _ = ls.accept()
            except socket.timeout:
                continue
            out(ev='accept', port=port)
            threading.Thread(target=conn, args=(c, port, kind), daemon=True).start()
        ls.close()

    ts = [threading.Thread(target=serve, args=(int(args[k]), kind), daemon=True)
          for k, kind in (('pm', 'mqtt'), ('p1', 'tls'), ('p2', 'https'))]
    for t in ts:
        t.start()
    out(ev='listen')
    for t in ts:
        t.join()


if __name__ == '__main__':
    mode = sys.argv[1]
    a = dict(x.split('=', 1) for x in sys.argv[2:])
    {'tcpserver': tcpserver, 'udp': udp, 'http': http, 'mcast': mcast, 'stuckpair': stuckpair, 'tlsserver': tlsserver,
     'https': https, 'mqttrelay': mqttrelay, 'disc': disc,
     'manyclients': manyclients, 'slowget': slowget, 'tlsset': tlsset}[mode](a)
