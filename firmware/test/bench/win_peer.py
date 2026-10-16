"""Bench peers on the Windows side (Plan 08a): the board dials Windows (WSL is behind NAT). Run by
bridge_check.py with Windows' Python; prints one JSON line per event, with ms since the start.

    python win_peer.py MODE key=value ...

MODE tcpserver  port, secs, delay_ms (listen only after), send_hex + send_ms (to each new connection,
                ms after its accept), drop_ms (close the first connection that long after its accept),
                modbus=1 (answer each Modbus TCP read with the registers 0x1234...), silent=1 (read, never answer)
     udp        port, secs, send_hex + send_ms + to=host:port
     http       port, secs, reply (the body)
     mcast      group, port, iface, send_hex, send_ms
     stuckpair  host, port, secs: two clients of a TCP server, one never reading, one logging what it got
     tlsserver  port, secs, cert, key (Windows paths), clientca (require a client certificate), send_hex +
                send_ms (after the handshake), big=N (one send of N bytes after the handshake), silent=1 (accept
                TCP, never answer the handshake); events hs (cipher, sni, peer), hsfail, data, close (Plan 08b)
     https      port, secs, cert, key, reply: the http mode over TLS
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
                tid, unit, fc, count = adu[0:2], adu[6], adu[7], struct.unpack('>H', adu[10:12])[0]
                regs = b''.join(struct.pack('>H', 0x1234 + i) for i in range(count))
                pdu = bytes([unit, fc, len(regs)]) + regs
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
            d, a = s.recvfrom(4096)
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


def mcast(args):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(args['iface']))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    time.sleep(float(args.get('send_ms', 0)) / 1000)
    s.sendto(bytes.fromhex(args['send_hex']), (args['group'], int(args['port'])))
    out(ev='sent')


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


if __name__ == '__main__':
    mode = sys.argv[1]
    a = dict(x.split('=', 1) for x in sys.argv[2:])
    {'tcpserver': tcpserver, 'udp': udp, 'http': http, 'mcast': mcast, 'stuckpair': stuckpair, 'tlsserver': tlsserver,
     'https': https}[mode](a)
