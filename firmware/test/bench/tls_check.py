#!/usr/bin/env python3
"""Plan 08b bench check: TLS on the bridge (spec §7.3) and the certificate check on upload (F20), the TLS
section of the ESP32's test_bridge.py against Windows peers (win_peer.py tlsserver/https). UART1's bridge is
under test, UART2 is the device side (E/F), as in bridge_check.py.

    python3 stm32/firmware/test/bench/tls_check.py [case ...]

Start from a wiped board (bench console "Z" then "WIPE"): there is no delete for certificates, and the
mutual case needs port1 without a key. Cases: certs tcpclient wrongca wrongname pending mutual https silent
twoports expired (last: the clock stays set until a restart). Prints one line per check and "bench: ok".
"""
import datetime
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402
import bridge_check as bc  # noqa: E402  (setup, base, peer, wait_peer, send_serial, last_hex, adc_pace, check)
import serial_check as sc  # noqa: E402
import web_check as w  # noqa: E402

PKI = '/mnt/c/Users/Public/sbiot/tls'
WPKI = r'C:\Users\Public\sbiot\tls' + '\\'
WIN, PORT = bc.WIN, 9443
check = bc.check


def make_pki():
    """The bench's certificates: server for 192.168.0.10 (RSA-2048, valid 2 days from an hour ago), etc.
    Kept between runs (the board holds the CA) while they have a day left."""
    ca = os.path.join(PKI, 'ca.pem')
    if os.path.exists(ca) and time.time() - os.path.getmtime(ca) < 86400:
        return
    here = os.path.dirname(os.path.abspath(__file__))
    subprocess.run([sys.executable, os.path.join(here, '..', 'host', 'mkpki.py'), PKI, '--now', '--ip', WIN,
                    '--wrong-ip', '192.168.0.99', '--days', '2'], check=True)


def pem(name):
    with open(os.path.join(PKI, name), 'rb') as f:
        return f.read()


def upload(scope, kind, body):
    s, _, d = w.req('POST', '/api/cert?scope=%s&kind=%s' % (scope, kind), body=body,
                    headers={'Content-Type': 'application/octet-stream'})
    try:
        return s, json.loads(d)
    except ValueError:
        return s, {}


def log_text():
    """The log since the last start (the RAM part): the flash part holds earlier boots' warnings."""
    s, _, d = w.req('GET', '/api/log')
    t = d.decode(errors='replace') if s == 200 else ''
    return t.split('# Since the last start', 1)[-1]


def log_num(pattern):
    m = re.findall(pattern, log_text())
    return int(m[-1]) if m else -1


def tls_port(**kw):
    f = bc.base(workMode=1, remoteHost=WIN, remotePort=PORT, tls=2, tlsVerify=1, reconnectInterval=2)
    f.update(kw)
    return f


def setup(**kw):
    upload('port1', 'ca', pem('ca.pem'))  # the run's CA, whatever an earlier run left
    return bc.setup(1, sys_extra={'ntp': 0}, **tls_port(**kw))  # the date rule needs the clock unset


def server(cert='server', **args):
    return bc.peer('tlsserver', port=PORT, cert=WPKI + cert + '.pem', key=WPKI + cert + '.key', **args)


def evs(ev, kind):
    return [e for e in ev if e.get('ev') == kind]


def serial_bytes(port=2):
    lines = sc.console([(0.3, 'F%d' % port)], 1.5)
    m = sc.find(lines, r'F%d frames (\d+) bytes (\d+)' % port)
    return int(m.group(2)) if m else -1


# ---- cases ---------------------------------------------------------------------------------------------

def case_certs():
    s, i = upload('port1', 'ca', pem('ca.pem'))
    check(s == 200 and 'CN=SBIOT Test CA' in i.get('subject', '') and len(i.get('validTo', '')) == 10,
          'the CA upload reports its subject and validTo (%s)' % i)
    check('expired' not in i and i.get('restartNeeded') is False, 'no clock: no "expired"; a port needs no restart')
    s, _ = upload('port1', 'ca', b'-----BEGIN CERTIFICATE-----\nnot base64 at all\n-----END CERTIFICATE-----\n')
    check(s == 400, 'a damaged certificate is refused (%d)' % s)
    s, _ = upload('port1', 'key', pem('ca.pem'))
    check(s == 400, 'a certificate sent as a private key is refused (%d)' % s)
    s, i = upload('mqtt', 'ca', pem('ca.pem'))
    check(s == 200 and i.get('restartNeeded') is True, 'mqtt: restartNeeded (%s)' % i)
    s, _ = upload('port2', 'cert', pem('client.pem'))
    s, i = upload('port2', 'key', pem('server.key'))
    check(s == 200 and i.get('pairMatches') is False, 'a key that does not match the client certificate is reported (%s)' % i)
    s, i = upload('port2', 'key', pem('client.key'))
    check(s == 200 and i.get('pairMatches') is True and i.get('keyType') == 'RSA', 'the right key matches, RSA (%s)' % i)
    room = log_num(r'stack headroom after the certificate check: (\d+)')
    check(room >= 1024, 'web: %d bytes of stack left after an RSA pair check' % room)


def case_tcpclient():
    p = server(secs=25, send_hex=b'down-tls'.hex(), send_ms=3000)
    check(setup(), 'set up UART1: TLS TCP client, tlsVerify 1, the clock unset')
    bc.last_hex()
    time.sleep(2)
    st = bc.port_state(1)
    bc.send_serial(b'secret')
    time.sleep(4)
    n, h = bc.last_hex()
    ev = bc.wait_peer(p)
    hs = evs(ev, 'hs')
    check(hs and hs[0]['version'] == 'TLSv1.2', 'TLS 1.2 session (%s)' % (hs[0] if hs else evs(ev, 'hsfail')))
    check(hs and hs[0].get('sni') == WIN, 'the dialled IP went out as SNI (the known limit, spec §7.3): %s' % (hs[0].get('sni') if hs else '-'))
    check(b'secret' in bc.data_of(ev), 'TLS: serial -> server')
    check(h and bytes.fromhex(h).endswith(b'down-tls'), 'TLS: server -> serial (%s)' % h)
    check('CONNECTED' in str(st), 'state CONNECTED (%s)' % st)
    log = log_text()
    check('certificate dates not checked: the clock is not set yet' in log, 'no clock: the dates skipped, with a warning')
    room = log_num(r'UART1: stack headroom after the TLS handshake: (\d+)')
    check(room >= 1024, 'bridge1: %d bytes of stack left after an RSA-2048 handshake' % room)


def refused(cert, flag, what):
    p = server(cert=cert, secs=12)
    check(setup(), 'set up UART1: TLS TCP client (%s)' % what)
    time.sleep(1)
    bc.send_serial(b'leak?')
    ev = bc.wait_peer(p)
    check(not evs(ev, 'hs') and evs(ev, 'hsfail') and b'leak' not in bc.data_of(ev),
          '%s: no session, no data (%d attempts)' % (what, len(evs(ev, 'accept'))))
    check(len(evs(ev, 'accept')) >= 2, '%s: the port retries' % what)
    m = re.search(r'certificate rejected \(flags 0x([0-9a-f]+)\)', log_text())
    check(m and int(m.group(1), 16) & flag, '%s: flag 0x%x logged (%s)' % (what, flag, m.group(0) if m else '-'))


def case_wrongca():
    refused('other', 0x8, 'a server signed by another CA')


def case_wrongname():
    refused('wrongsan', 0x4, 'a certificate for 192.168.0.99 only')


def case_pending():
    check(setup(), 'set up UART1: TLS TCP client, the server sends 4 KB at once')
    serial_bytes()  # from zero, before the server listens: one connection, one 4 KB record
    p = server(secs=12, big=4096)
    time.sleep(8)
    got = serial_bytes()
    ev = bc.wait_peer(p)
    check(len(evs(ev, 'hs')) == 1 and got == 4096, '4 KB in one TLS record, then silence: all of it at UART2 (%d)' % got)


def case_mutual():
    upload('port1', 'cert', pem('client.pem'))
    p = server(secs=10, clientca=WPKI + 'ca.pem')
    check(setup(tlsVerify=2), 'set up UART1: mutual TLS, port1 has no key yet')
    ev = bc.wait_peer(p)
    check(not evs(ev, 'hs') and 'mutual TLS needs a client certificate and key' in log_text(),
          'no key: the line, and no session')
    s, i = upload('port1', 'key', pem('client.key'))
    check(s == 200 and i.get('pairMatches') is True, 'the key uploaded, it matches')
    p = server(secs=15, clientca=WPKI + 'ca.pem')
    time.sleep(5)  # the next connection reads the key: no restart
    bc.send_serial(b'mutual')
    ev = bc.wait_peer(p)
    hs = evs(ev, 'hs')
    check(hs and hs[0].get('peer') == 'gateway' and b'mutual' in bc.data_of(ev),
          'tlsVerify 2: the server sees CN=gateway, the data flows (%s)' % (hs[0] if hs else evs(ev, 'hsfail')))


def case_https():
    p = bc.peer('https', port=9444, secs=15, cert=WPKI + 'server.pem', key=WPKI + 'server.key', reply='OK-HTTPS')
    check(bc.setup(1, sys_extra={'ntp': 0}, **bc.base(workMode=4, httpHost=WIN, httpPort=9444, httpMethod=0, httpPath='/q?v=',
                                                      httpHeaders='', httpStripHeaders=True, httpTimeout=10, tls=2, tlsVerify=1)),
          'set up UART1: HTTPS client, GET, tlsVerify 1')
    bc.last_hex()
    bc.send_serial(b'1')
    time.sleep(5)
    n, h = bc.last_hex()
    ev = bc.wait_peer(p)
    req = evs(ev, 'request')
    check(req and req[0]['path'] == '/q?v=1', 'HTTPS GET reached the server (%s)' % (req[0] if req else evs(ev, 'hsfail')))
    check(h and bytes.fromhex(h) == b'OK-HTTPS', 'HTTPS: the body to serial (%s)' % h)
    room = log_num(r'UART1: stack headroom after HTTPS: (\d+)')
    check(room >= 1024, 'bridge1: %d bytes of stack left after HTTPS' % room)


def do_pulse(seconds=12):
    """DO1's 2 s pulse (do0Mode 1), timed by the board's output trace (D)."""
    def pulse():
        time.sleep(3)
        w.req('POST', '/api/io', body=json.dumps({'do': 0, 'value': True}).encode())
    lines = [l for _, l in board.listen(seconds, sends=[(1.0, 'D'), (9.0, 'D')], during=pulse)]
    outs = [(int(m.group(2)), int(m.group(1), 16)) for l in lines for m in [re.search(r'outputs 0x(\w+) at (\d+) ms', l)] if m]
    on = [t for t, v in outs if v & 1]
    off = [t for t, v in outs if not v & 1 and on and t > on[0]]
    return off[0] - on[0] if on and off else -1


def case_silent():
    p = server(secs=40, silent=1)
    check(setup(reconnectInterval=1), 'set up UART1: TLS TCP client to a server that never answers')
    ev = bc.wait_peer(p)
    acc = [e['t'] for e in evs(ev, 'accept')]
    gaps = [b - a for a, b in zip(acc, acc[1:])]
    check(gaps and all(10000 <= g <= 12500 for g in gaps), 'the handshake gives up after 10 s, then retries (%s ms)' % gaps)
    # handshakes failing in a loop (another CA, RSA-2048, every second): IO keeps its pace
    _, io = w.get_json('/api/config/io')
    io.update({'do0Mode': 1, 'do0FlipDelay': 2})
    w.req('POST', '/api/config/io', body=json.dumps(io).encode())
    p = server(cert='other', secs=50)
    check(setup(reconnectInterval=1), 'set up UART1: a refused handshake every second, DO1 pulse 2 s')
    idle0 = bc.adc_pace()
    time.sleep(5)
    a0 = bc.adc_pace()
    time.sleep(25)
    a1 = bc.adc_pace()
    pulse = do_pulse()
    ev = bc.wait_peer(p)
    wins = (a1[1] - a0[1]) / (a1[0] - a0[0]) if a0 and a1 else -1
    late = a1[2] - a0[2] if a0 and a1 else -1
    check(len(evs(ev, 'accept')) >= 15, '%d refused handshakes in the window' % len(evs(ev, 'accept')))
    check(9.5 <= wins <= 10.5 and late == 0 and 1980 <= pulse <= 2020,
          'under failing handshakes: ADC %.2f windows/s, late +%d; DO pulse %d ms (idle start %s)' % (wins, late, pulse, idle0))
    io.update({'do0Mode': 0})
    w.req('POST', '/api/config/io', body=json.dumps(io).encode())


def heap():
    _, st = w.get_json('/api/status')
    return (st['system']['heapFree'], st['system']['heapMin']) if st else (-1, -1)


def case_twoports():
    _, p2 = w.get_json('/api/config/port2')
    keep = dict(p2)
    p2.update({'workMode': 4, 'httpHost': WIN, 'httpPort': 9444, 'httpMethod': 0, 'httpPath': '/n?v=', 'httpHeaders': '',
               'httpStripHeaders': True, 'httpTimeout': 10, 'tls': 2, 'tlsVerify': 1})
    w.req('POST', '/api/config/port2', body=json.dumps(p2).encode())
    upload('port2', 'ca', pem('ca.pem'))
    check(bc.setup(1, sys_extra={'ntp': 0}, **bc.base()), 'set up: UART1 TCP server (no TLS), UART2 HTTPS client')
    h0 = heap()
    p = server(secs=70)
    hp = bc.peer('https', port=9444, secs=70, cert=WPKI + 'server.pem', key=WPKI + 'server.key', reply='OK')
    check(setup(), 'set up UART1: TLS TCP client too')
    time.sleep(4)
    h1 = heap()
    for i in range(15):  # UART1 -> the RS485 loop -> UART2: an HTTPS request every 2.4 s. Closer, UART2's
        sc.console([(0.2, 'E1 4 %s' % (b'r%02d' % i).hex())], 0.8)  # reply collides with UART1's next frame
        time.sleep(1.2)                                                # on the half-duplex loop (bench only)
    time.sleep(3)
    h2 = heap()
    ev = bc.wait_peer(p)
    hev = bc.wait_peer(hp)
    reqs = evs(hev, 'request')
    check(evs(ev, 'hs') and len(reqs) == 15, 'both work: port1 in session, %d/15 HTTPS requests from port2' % len(reqs))
    session = h0[0] - h1[0]
    peak = h0[0] - h2[1]
    print('     heap: free %d with no session, %d with port1 in session (%d B), minimum %d (peak use %d B over idle)'
          % (h0[0], h1[0], session, h2[1], peak))
    check(0 < session <= 45000 and peak <= 2 * 45000 + 35000,
          'a session costs %d B (spec §5.3: about 35 KB), the peak %d B over idle' % (session, peak))
    w.req('POST', '/api/config/port2', body=json.dumps(keep).encode())


def case_expired():
    _, sysc = w.get_json('/api/config/system')
    tz = int(sysc.get('timezone', 0)) if sysc else 0
    later = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=5, hours=tz)
    check(setup(), 'set up UART1: TLS TCP client (no server yet)')
    s, _, _ = w.req('POST', '/api/time', body=json.dumps({'time': later.strftime('%Y-%m-%d %H:%M:%S')}).encode())
    check(s == 200, 'the clock set to 5 days from now')
    p = server(secs=12)  # every handshake comes after the clock was set
    ev = bc.wait_peer(p)
    check(not evs(ev, 'hs') and evs(ev, 'hsfail') and re.search(r'certificate rejected \(flags 0x1\)', log_text()),
          'clock set: the 2-day server certificate has expired and is refused (%d refusals)' % len(evs(ev, 'hsfail')))


CASES = {'certs': case_certs, 'tcpclient': case_tcpclient, 'wrongca': case_wrongca, 'wrongname': case_wrongname,
         'pending': case_pending, 'mutual': case_mutual, 'https': case_https, 'silent': case_silent,
         'twoports': case_twoports, 'expired': case_expired}


def main():
    if not w.wait_up(5):
        print('the board does not answer on %s' % bc.GW)
        return 1
    make_pki()
    for name in sys.argv[1:] or CASES:
        CASES[name]()
    print('bench: %s' % ('ok' if not bc.failures else 'FAILED (%d)' % len(bc.failures)))
    return 1 if bc.failures else 0


if __name__ == '__main__':
    sys.exit(main())
