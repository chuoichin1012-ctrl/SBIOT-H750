"""Plan 12 bench: FinderTool's protocol against the board (spec 2026-10-17-lan-discovery-design.md §4).
Windows sends the broadcasts (WSL is behind NAT): win_peer.py disc. Never sends `factory` (the SIM is locked:
a factory reset sets priority 1 and LTE would attach). Leaves the board at 192.168.0.7.

    python3 disc_check.py
"""
import json
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_check as bc  # noqa: E402  (peer, wait_peer, WIN)
import web_check as w  # noqa: E402

SN = 'SB2610000001'  # the bench board's record (memory: bench-board-state)
failures = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def new_id():
    return '%08x' % random.getrandbits(32)


def send(pkt, secs=3):
    """Broadcasts pkt from Windows; the JSON replies with its id."""
    text = json.dumps(pkt, separators=(',', ':'))
    ev = bc.wait_peer(bc.peer('disc', iface=bc.WIN, send=text.encode().hex(), secs=secs))
    out = []
    for e in ev:
        if e.get('ev') == 'reply':
            try:
                r = json.loads(e['text'])
            except ValueError:
                continue
            if r.get('id') == pkt['id']:
                out.append(r)
    return out


def search(mac=None):
    p = {'sbiot': 'disc', 'v': 1, 'op': 'search', 'id': new_id()}
    if mac:
        p['mac'] = mac
    return [r for r in send(p) if r.get('op') == 'found']


def command(op, mac, net=None):
    p = {'sbiot': 'disc', 'v': 1, 'op': op, 'id': new_id(), 'mac': mac}
    if net is not None:
        p['net'] = net
    acks = [r for r in send(p, secs=10) if r.get('op') == 'ack']
    return acks[0] if acks else None


def back(mac, boot, limit=120):
    """Searches every 3 s until mac answers with a larger boot: its found, or None."""
    t = time.time()
    while time.time() - t < limit:
        for f in search(mac):
            if f['boot'] > boot:
                return f
        time.sleep(3)
    return None


def restart_and_wait(mac, boot):
    w.req('POST', '/api/restart')
    return back(mac, boot)


def main():
    _, orig = w.get_json('/api/config/network')
    if not orig or orig.get('priority') != 0:
        print('bench: the board must start at network.priority 0 (the SIM is locked); found %s' % (orig or {}).get('priority'))
        return 1
    f = search()
    me = [x for x in f if x.get('sn') == SN]
    check(len(me) == 1, 'search finds the board (SN %s)' % SN)
    if not me:
        return 1
    me = me[0]
    mac, boot = me['mac'], me['boot']
    home = me['net']  # the board's own saved settings: put back as they were
    away = dict(home, ipMode=1, ip='10.10.0.7', netmask='255.255.255.0', gateway='10.10.0.1')
    check(me['eth']['ip'] == '192.168.0.7' and home['ip'] == '192.168.0.7' and me['web'] == 80 and me['model'] == 'SBIOT-H750',
          'found: running and saved addresses, web port, model (%s)' % json.dumps(me))
    check(all(k in me for k in ('fw', 'hostname')), 'found: fw and hostname')
    check(search('02:00:00:00:00:01') == [], 'a search for another MAC: no reply')

    a = command('set', mac, dict(home, ip='10.10.0.256'))
    check(a is not None and a.get('ok') is False and a.get('err') == 'bad', 'set with a wrong address: ack bad (%s)' % a)
    p = {'sbiot': 'disc', 'v': 1, 'op': 'restart', 'id': new_id()}
    check(send(p) == [], 'restart without mac: ignored')
    time.sleep(3)
    f = search(mac)
    check(f and f[0]['boot'] == boot, 'and the board did not restart')

    try:
        a = command('set', mac, away)
        check(a is not None and a.get('ok') is True, 'set 10.10.0.7/24 (another subnet than Windows): ack ok')
        f = back(mac, boot)
        check(f is not None and f['eth']['ip'] == '10.10.0.7' and f['net'] == away,
              'found again at 10.10.0.7 after its restart, from 192.168.0.10')
        boot = f['boot'] if f else boot
    finally:
        a = command('set', mac, home)
        check(a is not None and a.get('ok') is True, 'set back to 192.168.0.7: ack ok')
        f = back(mac, boot)
        check(f is not None and f['eth']['ip'] == '192.168.0.7' and f['net'] == home, 'found again at 192.168.0.7, settings as before')
        boot = f['boot'] if f else boot
    _, net = w.get_json('/api/config/network')
    check(net == orig, 'network is exactly as before the bench: LTE fields, priority and the rest kept')

    # A flood of junk on 48600 for 70 s, longer than syslog's 60 s supervisor deadline (the lowest task, priority 2):
    # if disc starved the tasks below it, the supervisor would restart the board (Codex plan r2).
    flood = bc.subprocess.run([bc.WIN_PY, '-c', (
        'import socket,time\n'
        's=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)\n'
        's.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1)\n'
        's.bind(("%s",0))\n'
        't=time.time();n=0\n'
        'while time.time()-t<70:\n'
        '    s.sendto(b"x"*100,("255.255.255.255",48600));n+=1\n'
        'print(n)\n' % bc.WIN)], cwd='/mnt/c', capture_output=True, text=True)
    sent = int(flood.stdout.strip() or 0) if flood.returncode == 0 else 0
    check(sent > 50000, 'the flood ran: %d datagrams in 70 s (exit %d)' % (sent, flood.returncode))
    f = search(mac)
    check(f and f[0]['boot'] == boot, 'after it the board answers, not restarted (no task starved past its deadline)')

    # A UART set (around the UI) to bind UDP 48600: disc bound first at boot, so the board is still found.
    _, p1 = w.get_json('/api/config/port1')
    try:
        w.req('POST', '/api/config/port1', body=json.dumps(dict(p1, workMode=2, localPort=48600)).encode())
        f = restart_and_wait(mac, boot)
        check(f is not None, 'with UART1 configured as a UDP server on 48600, the board is still found after a restart')
        boot = f['boot'] if f else boot
        _, st = w.get_json('/api/status')
        state = ((st or {}).get('ports') or [{}])[0].get('state')
        check(state == 'ERROR', "and UART1's bridge reports its bind error (state %s)" % state)
    finally:
        w.req('POST', '/api/config/port1', body=json.dumps(p1).encode())
        f = restart_and_wait(mac, boot)
        boot = f['boot'] if f else boot
    _, now1 = w.get_json('/api/config/port1')
    check(now1 == p1, 'UART1 restored')

    a = command('restart', mac)
    check(a is not None and a.get('ok') is True, 'restart: ack ok')
    f = back(mac, boot)
    check(f is not None, 'and the board comes back with a new boot')
    print('bench: ' + ('FAILED (%d)' % len(failures) if failures else 'ok'))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
