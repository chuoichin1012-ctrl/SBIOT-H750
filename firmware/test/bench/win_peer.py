"""Bench peers on the Windows side (Plan 08a): the board dials Windows (WSL is behind NAT). Run by
bridge_check.py with Windows' Python; prints one JSON line per event, with ms since the start.

    python win_peer.py MODE key=value ...

MODE tcpserver  port, secs, delay_ms (listen only after), send_hex + send_ms (to each new connection,
                ms after its accept), drop_ms (close the first connection that long after its accept),
                modbus=1 (answer each Modbus TCP read with the registers 0x1234...), silent=1 (read, never answer)
     udp        port, secs, send_hex + send_ms + to=host:port
     http       port, secs, reply (the body)
     mcast      group, port, iface, send_hex, send_ms
"""
import json
import socket
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


def mcast(args):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(args['iface']))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    time.sleep(float(args.get('send_ms', 0)) / 1000)
    s.sendto(bytes.fromhex(args['send_hex']), (args['group'], int(args['port'])))
    out(ev='sent')


if __name__ == '__main__':
    mode = sys.argv[1]
    a = dict(x.split('=', 1) for x in sys.argv[2:])
    {'tcpserver': tcpserver, 'udp': udp, 'http': http, 'mcast': mcast}[mode](a)
