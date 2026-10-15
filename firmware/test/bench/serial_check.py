#!/usr/bin/env python3
"""Plan 07a bench check: the serial ports (spec §6.5), with the board's two RS485 ports wired to each
other (A to A, B to B). UART2 is UART1's counterpart, timed by the board itself (bench console E/F,
app/bench_serial.c), so the gaps on the wire are the board's own and not Windows' buffering.

    python3 stm32/firmware/test/bench/serial_check.py

Needs the board running this build's app, its LAN reachable at 192.168.0.7 (factory login), UART7 on
the bench console. Cases:
1. the WebSocket console in UART1 mode: WebSocket -> UART1 -> UART2 (echo) -> UART1 -> WebSocket, byte
   for byte, at 9600 8N1 (1 and 100 bytes), 230 400 8E1 (1460 bytes) and 600 7O2 (20 bytes);
   packetLength 100 cuts an echoed 300-byte frame in three; packetInterval keeps or cuts a reply sent
   in two halves 30 ms apart (50: one frame, 10: two);
2. a Modbus transaction at 1200 8E1 (t1.5 13.75 ms, t3.5 32.08 ms): a good answer; an answer in two
   halves 20 ms apart is dropped as a t1.5 violation by the board's own count; 60 ms apart (beyond
   t3.5) is two frames and no violation; the next good answer after a violation comes in; an answer
   from another slave goes to the subscribers;
3. 60 s of continuous RX at 230 400 on UART1 while the LAN is flooded: every byte counted, 0 overrun;
4. a parity mismatch: frames dropped and counted as line errors;
5. XON/XOFF at 230 400: an XOFF before a write holds it, the XON lets it through whole; an XOFF with
   no XON fails the write at its deadline, and the next write works; an XOFF injected 20 ms into a
   2 KB write (bench X) pauses the running DMA for about 1 s, and every byte arrives.
Prints one line per check and "bench: ok"; exits 1 on any failure.
"""
import base64
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
import web_check as w  # noqa: E402  (HTTP and WebSocket helpers)

WIN_PY = '/mnt/c/Python311/python.exe'
TOKEN = base64.b64encode(b'admin:admin').decode()
failures = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def console(sends, seconds):
    """Bench console lines; sends: [(t, text)]. E/F/T/X take the rest of the line after the letter."""
    out = []
    for t, s in sends:
        out.append((t, s[0]))
        if len(s) > 1:
            out.append((t + 0.1, s[1:] + '\r'))
    return [l for _, l in board.listen(seconds, sends=out)]


def find(lines, pattern):
    for l in lines:
        m = re.search(pattern, l)
        if m:
            return m
    return None


def counters(port):
    m = find(console([(0.3, 's')], 1.5), r'serial %d tx (\d+) rx (\d+) overrun (\d+) line (\d+) viol (\d+) dropped (\d+) lost (\d+)' % port)
    keys = ['tx', 'rx', 'overrun', 'line', 'viol', 'dropped', 'lost']
    return dict(zip(keys, map(int, m.groups()))) if m else {}


def frames(port):
    """F<port>: (count, bytes, [(len, gap ms)])."""
    m = find(console([(0.3, 'F%d' % port)], 1.5), r'F%d frames (\d+) bytes (\d+):(.*)' % port)
    if not m:
        return 0, 0, []
    items = [tuple(map(int, x.split('+'))) for x in m.group(3).split()]
    return int(m.group(1)), int(m.group(2)), items


def setup(line1, line2=None, target=0, extra1=None):
    """Both ports' line settings (and the console target), saved, then a restart."""
    line2 = line2 or line1
    for n, ln, ex in ((1, line1, extra1), (2, line2, None)):
        cfg = dict(zip(('baudRate', 'dataBits', 'parity', 'stopBits'), ln))
        cfg.update({'packetLength': 0, 'packetInterval': 0, 'flowControl': 0})
        cfg.update(ex or {})
        w.req('POST', '/api/config/port%d' % n, body=json.dumps(cfg).encode())
    _, sysc = w.get_json('/api/config/system')
    sysc['websocketTarget'] = target
    w.req('POST', '/api/config/system', body=json.dumps(sysc).encode())
    ok = w.restart()
    time.sleep(1)
    return ok


def ws_send_bin(s, data):
    mask = os.urandom(4)
    n = len(data)
    hdr = bytes([0x82, 0x80 | n]) if n < 126 else bytes([0x82, 0xFE]) + struct.pack('>H', n)
    s.sendall(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))


def ws_recv_bin(s, seconds):
    """Payloads of the binary frames received within `seconds`."""
    s.settimeout(0.2)
    buf, out, end = b'', [], time.time() + seconds
    while time.time() < end:
        try:
            d = s.recv(65536)
            if not d:
                break
            buf += d
        except socket.timeout:
            pass
        while len(buf) >= 2:
            op, n, off = buf[0] & 0x0F, buf[1] & 0x7F, 2
            if n == 126:
                if len(buf) < 4:
                    break
                n, off = struct.unpack('>H', buf[2:4])[0], 4
            if len(buf) < off + n:
                break
            if op == 2:
                out.append(buf[off:off + n])
            buf = buf[off + n:]
    return out


def echo_case(label, data, wait):
    ws, st = w.ws_open(6432, TOKEN)
    w.ws_recv_all(ws, 0.5)  # the greeting
    ws_send_bin(ws, data)
    got = b''.join(ws_recv_bin(ws, wait))
    ws.close()
    check(got == data, '%s: %d bytes echoed through UART1 and UART2 (%d back)' % (label, len(data), len(got)))


def case_console():
    for line, label, sizes in (((9600, 8, 0, 1), '9600 8N1', (1, 100)), ((230400, 8, 2, 1), '230 400 8E1', (1460,)),
                               ((600, 7, 1, 2), '600 7O2', (20,))):
        check(setup(line, target=1), 'set up %s, console on UART1' % label)
        console([(0.3, 'E2 1')], 1)
        for n in sizes:
            data = bytes((i * 37 + 11) & (0x7F if line[1] == 7 else 0xFF) for i in range(n))
            echo_case(label, data, 3 + n * 11.0 * 2 / line[0])
    # packetLength
    check(setup((115200, 8, 0, 1), target=1, extra1={'packetLength': 100}), 'set up packetLength 100 on UART1')
    console([(0.3, 'E2 1')], 1)
    ws, _ = w.ws_open(6432, TOKEN)
    w.ws_recv_all(ws, 0.5)
    data = bytes(range(256)) + bytes(range(44))
    ws_send_bin(ws, data)
    got = ws_recv_bin(ws, 2)
    ws.close()
    check([len(x) for x in got] == [100, 100, 100] and b''.join(got) == data,
          'packetLength 100: a 300-byte echo in three frames (%s)' % [len(x) for x in got])
    # packetInterval: UART2 answers any frame in two halves 30 ms apart
    for interval, want in ((50, 1), (10, 2)):
        setup((115200, 8, 0, 1), extra1={'packetInterval': interval})
        console([(0.3, 'E2 2 30 ' + '55' * 40), (0.8, 'F1')], 1.5)
        console([(0.3, 'E1 4 0203')], 2)  # raw bytes from UART1: UART2 answers them in two halves
        n, b, items = frames(1)
        answer = items
        check(len(answer) == want and sum(x[0] for x in answer) == 40,
              'packetInterval %d with halves 30 ms apart: %d frame(s) (%s)' % (interval, want, items))


def case_modbus():
    check(setup((1200, 8, 2, 1)), 'set up 1200 8E1')
    good = '0103040001000213fb'  # an answer of slave 1
    req = '010300000002c40b'
    def transact():
        return console([(0.3, 'T1 ' + req)], 2.5)
    console([(0.3, 'E2 2 0 ' + good)], 1)
    m = find(transact(), r'T1 answer (\d+) bytes .*: (\w+)')
    check(m is not None and m.group(2) == good, 'a good answer (%s)' % (m.group(0) if m else '-'))
    v0 = counters(1).get('viol', -1)
    console([(0.3, 'E2 2 20 ' + good)], 1)
    m = find(transact(), r'T1 dropped: t1\.5 violation')
    v1 = counters(1).get('viol', -1)
    check(m is not None and v1 == v0 + 1, 'halves 20 ms apart (t1.5 < gap < t3.5): dropped, the board counts a violation (%d -> %d)' % (v0, v1))
    console([(0.3, 'E2 2 60 ' + good), (0.9, 'F1')], 1.5)
    lines = transact()
    m = find(lines, r'T1 answer (\d+) bytes')
    v2 = counters(1).get('viol', -1)
    n, b, items = frames(1)
    check(m is not None and int(m.group(1)) == 4 and v2 == v1 and b == 5,
          'halves 60 ms apart (beyond t3.5): two frames, no violation; the answer is the first, the rest goes to the subscribers (%s, %s)'
          % (m.group(0) if m else '-', items))
    console([(0.3, 'E2 2 20 ' + good)], 1)
    transact()
    console([(0.3, 'E2 2 0 ' + good)], 1)
    m = find(transact(), r'T1 answer (\d+) bytes .*: (\w+)')
    check(m is not None and m.group(2) == good, 'after a violation, the next good answer comes in (resync)')
    console([(0.3, 'E2 2 0 0203040001000213fb'), (0.9, 'F1')], 1.5)
    lines = transact()
    n, b, items = frames(1)
    check(find(lines, r'T1 no answer') is not None and n == 1 and b == 9, 'an answer from slave 2: no answer, the frame goes to the subscribers (%s)' % items)
    console([(0.3, 'E2 0')], 1)


def case_continuous():
    check(setup((230400, 8, 0, 1)), 'set up 230 400 8N1')
    c1, c2 = counters(1), counters(2)
    flood = subprocess.Popen([WIN_PY, '-c', (
        'import socket,time\n'
        's=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)\n'
        'time.sleep(15);t=time.time()\n'
        'while time.time()-t<20:\n'
        '    s.sendto(b"x"*1000,("192.168.0.7",9))\n')])
    lines = console([(0.3, 'E2 3 60')], 66)
    flood.wait()
    m = find(lines, r'E2 streamed (\d+) bytes')
    d1, d2 = counters(1), counters(2)
    sent = d2['tx'] - c2['tx']
    got = d1['rx'] - c1['rx']
    check(m is not None and sent > 230400 // 11 * 50 and got == sent and d1['overrun'] == c1['overrun'] and d1['lost'] == c1['lost']
          and d1['line'] == c1['line'],
          '60 s at 230 400 with a 20 s LAN flood: %d sent, %d received, overrun +%d, lost +%d' %
          (sent, got, d1.get('overrun', 0) - c1.get('overrun', 0), d1.get('lost', 0) - c1.get('lost', 0)))


def case_parity():
    check(setup((9600, 8, 2, 1), (9600, 8, 1, 1)), 'set up UART1 8E1, UART2 8O1')
    c = counters(1)
    console([(0.3, 'F1'), (0.8, 'T2 0203000000010000')], 2.5)
    d = counters(1)
    n, b, items = frames(1)
    check(d['line'] > c['line'] and n == 0, 'a parity mismatch: line errors +%d, no frame' % (d['line'] - c['line']))


def case_xonxoff():
    check(setup((230400, 8, 0, 1), extra1={'flowControl': 2}, target=1), 'set up 230 400, XON/XOFF on UART1, console on UART1')
    data = bytes((i * 7) & 0xFF for i in range(2048))
    console([(0.3, 'E2 0'), (0.8, 'F2')], 1.5)
    console([(0.3, 'E2 4 13')], 1)  # XOFF from the device, before the write (half duplex: never during it)
    ws, _ = w.ws_open(6432, TOKEN)
    w.ws_recv_all(ws, 0.5)
    ws_send_bin(ws, data)
    time.sleep(0.3)
    # within the write's deadline (the console's 2 s, plus 2 character times a byte): look, then XON
    m = find(console([(0.3, 'F2'), (0.7, 'E2 4 11')], 1.5), r'F2 frames (\d+) bytes (\d+)')
    b0 = int(m.group(2)) if m else -1
    n1, b1, _ = frames(2)
    check(b0 == 0 and b1 == 2048, 'XOFF holds a 2 KB write (%d bytes meanwhile), XON lets it through (%d)' % (b0, b1))
    console([(0.3, 'E2 4 13')], 1)
    ws_send_bin(ws, data)
    time.sleep(4)  # past the write's deadline: 2 s plus 2 character times a byte
    n2, b2, _ = frames(2)
    console([(0.3, 'E2 4 11')], 1)
    ws_send_bin(ws, data[:100])
    time.sleep(1)
    n3, b3, _ = frames(2)
    check(b2 == 0 and b3 == 100, 'XOFF and no XON: the write fails at its deadline (%d bytes); after XON the next one works (%d)' % (b2, b3))
    console([(0.3, 'X1')], 1)
    ws_send_bin(ws, data)
    time.sleep(3)
    n4, b4, items = frames(2)
    gaps = [g for _, g in items]
    check(b4 == 2048 and n4 >= 2 and max(gaps) > 900, 'XOFF injected into a running 2 KB write: a pause of %d ms, all %d bytes' % (max(gaps) if gaps else 0, b4))
    ws.close()


def main():
    if not w.wait_up(5):
        print('the board does not answer on %s' % w.HOST)
        return 1
    case_console()
    case_modbus()
    case_parity()
    case_xonxoff()
    case_continuous()
    setup((115200, 8, 0, 1))  # the factory line settings again
    print('bench: %s' % ('ok' if not failures else 'FAILED (%d)' % len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
