#!/usr/bin/env python3
"""Plan 06a bench check: Ethernet and lwIP on the board (spec §5.6, §6.1, §7.1); since Plan 11b on the bench's
routed LAN (bench_env: the board's address found by FinderTool's search, DHCP or static as saved).

    python3 stm32/firmware/test/bench/net_check.py

WSL2 runs in NAT mode, so the pings go through Windows (ping.exe) and the flood runs from Windows Python. Every
`network` saved is the board's whole saved document with only the Ethernet fields changed (priority kept: a
partial document would take the default 1 and wake the LTE modem; Codex), saved over the web. Cases:
1. the saved settings (HOME): the link at 100 Mbit full duplex, pings, a 3000-byte ping (fragmented,
   reassembled by lwIP), the PHY found;
2. a static address (SBIOT_STATIC, default 192.168.1.77, checked free first) after a restart: it answers, the
   home address does not, the status shows the running values; then HOME again (found again by the search);
3. the link dropped five times (the PHY powered down from the console): it comes back each time,
   pings answer, and the RX buffers in use return to the baseline;
4. a 20 s UDP flood: the watchdog task keeps its period, the console answers, the CPU still idles;
   afterwards pings answer within 2 s and the RX buffers return to the baseline.
Prints one line per check, the measurements, and "bench: ok"; exits 1 on any failure.
"""
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_env  # noqa: E402
import board  # noqa: E402
import web_check as w  # noqa: E402

WIN_PY = bench_env.WIN_PY
STATIC = os.environ.get('SBIOT_STATIC', '192.168.1.77')
failures = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def find(lines, pattern):
    for _, line in lines:
        m = re.search(pattern, line)
        if m:
            return m
    return None


def ping(ip, count=4, size=32, wait_ms=1000):
    """Replies received, from Windows."""
    out = subprocess.run(['ping.exe', '-n', str(count), '-l', str(size), '-w', str(wait_ms), ip],
                         capture_output=True, text=True).stdout
    return len(re.findall(r'Reply from %s: bytes=' % re.escape(ip), out))


def status():
    lines = board.listen(2, sends=[(0.3, 'n')])
    return lines, find(lines, r'rx_in_use (\d+)')


def restart_with(net):
    """Saves the whole `network` document `net` over the web, restarts by NRST; returns the boot's lines, up to the
    link at 100 Mbit and a DHCP lease."""
    st, _, d = w.req('POST', '/api/config/network', body=json.dumps(net).encode())
    check(st == 200, 'network config saved (%d %s)' % (st, d[:60]))
    return board.listen(16, during=board.hard_reset)


def follow():
    """The board's address after a restart, by the search; every client follows it."""
    for _ in range(10):
        found = bench_env.find_board(bench_env.WIN, bench_env.SN)
        if found:
            bench_env.set_board(found['eth']['ip'])
            return found
        time.sleep(2)
    return None


def main():
    _, home = w.get_json('/api/config/network')
    if not home or home.get('priority') != 0:
        print('bench: the board must start at network.priority 0 (the SIM is locked); found %s' % (home or {}).get('priority'))
        return 1
    if ping(STATIC, 2, wait_ms=500):
        print('bench: %s answers already: set SBIOT_STATIC to a free address' % STATIC)
        return 1
    _, st0 = w.get_json('/api/status')
    eth0 = (st0 or {}).get('eth', {})
    print('     home: %s (%s), saved ipMode %s' % (bench_env.GW, 'DHCP' if home.get('ipMode') == 0 else 'static', home.get('ipMode')))
    try:
        # 1. The saved settings
        lines = restart_with(home)
        found = follow()
        gw = found['eth']['ip'] if found else bench_env.GW
        phy = find(lines, r'net: PHY at (\d+), id ([0-9a-f]+)')
        check(phy is not None and phy.group(2) == '02430c54', 'IP101 found (%s)' % (phy.group(0) if phy else '-'))
        check(find(lines, r'net: (DHCP|static)') is not None, 'the address mode from the config')
        up = find(lines, r'net: link (up|now), 100 Mbit full duplex')
        check(up is not None, 'link up at 100 Mbit full duplex')
        check(found is not None, 'found again at %s' % gw)
        n = ping(gw, 20)
        check(n == 20, '20 pings, %d answered' % n)
        n = ping(gw, 4, 3000)
        check(n == 4, 'a 3000-byte ping (fragmented): %d of 4' % n)
        lines, rx = status()
        base = int(rx.group(1)) if rx else -1
        print('     RX buffers in use at rest: %d' % base)

        # 2. A static address, after a restart
        mask = found['eth']['netmask'] if found else eth0.get('mask', '255.255.255.0')
        router = found['eth']['gateway'] if found else eth0.get('gateway', '192.168.1.1')
        other = dict(home, ipMode=1, ip=STATIC, netmask=mask, gateway=router, dnsMode=1, dns1='9.9.9.9', dns2='1.0.0.1')
        lines = restart_with(other)
        f2 = follow()
        check(f2 is not None and f2['eth']['ip'] == STATIC and ping(STATIC, 4) == 4 and ping(gw, 2, wait_ms=500) == 0,
              '%s answers after the restart, %s does not' % (STATIC, gw))
        lines, _ = status()
        check(find(lines, r'static %s mask %s gw %s dns 9\.9\.9\.9 1\.0\.0\.1' % tuple(map(re.escape, (STATIC, mask, router)))) is not None,
              'the status shows the running values')
        restart_with(home)
        f3 = follow()
        gw = f3['eth']['ip'] if f3 else gw
        check(f3 is not None and ping(gw, 4) == 4, 'the saved settings again (%s)' % gw)

        # 3. Link drops
        bad = 0
        for i in range(5):
            lines = board.listen(16, sends=[(0.3, 'L')])
            down = find(lines, r'net: link down')
            back = find(lines, r'net: link (up|now), 100 Mbit')
            time.sleep(3)  # a DHCP lease is taken again after the link
            ok = down is not None and back is not None and ping(gw, 3) == 3
            bad += not ok
        lines, rx = status()
        check(bad == 0, 'five link drops: down, up again, pings answer')
        check(rx is not None and int(rx.group(1)) == base, 'RX buffers back to %d (%s)' % (base, rx.group(1) if rx else '-'))

        # 4. A UDP flood from Windows, to a closed port: each datagram also costs the board an ICMP reply.
        flood = subprocess.Popen([WIN_PY, '-c', (
            'import socket,time\n'
            's=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)\n'
            't=time.time();n=0\n'
            'while time.time()-t<20:\n'
            '    s.sendto(b"x"*1000,("%s",9));n+=1\n'
            'print(n)\n' % gw)], cwd='/mnt/c', stdout=subprocess.PIPE, text=True)
        time.sleep(2)
        during = board.listen(16, sends=[(1.0, 'P'), (2.0, 'i'), (14.0, 'P')])
        sent = flood.communicate()[0].strip()
        idle2 = [re.search(r'idle_us=(\d+) now_us=(\d+)', l) for _, l in during]
        idle2 = [m for m in idle2 if m]
        gap = find(during, r'wdog_max_gap_us=(\d+)')
        check(gap is not None and int(gap.group(1)) < 400000, 'flood: wdog gap %s us < 400 ms' % (gap.group(1) if gap else '-'))
        check(len(idle2) >= 2, 'flood: the console answers')
        if len(idle2) >= 2:
            a, b = idle2[0], idle2[-1]
            frac = (int(b.group(1)) - int(a.group(1))) / max(1, int(b.group(2)) - int(a.group(2)))
            print('     %s datagrams in 20 s over Wi-Fi; CPU idle during the flood: %.1f %%' % (sent, frac * 100))
        tic = time.time()
        n = ping(gw, 2)
        check(n >= 1 and time.time() - tic < 3, 'pings answer right after the flood (%d of 2)' % n)
        lines, rx = status()
        stl = find(lines, r'rx_starved (\d+) tx_busy_drops (\d+)')
        check(rx is not None and int(rx.group(1)) == base, 'RX buffers back to %d after the flood (%s)' % (base, rx.group(1) if rx else '-'))
        print('     rx_starved %s, tx_busy_drops %s' % (stl.group(1) if stl else '-', stl.group(2) if stl else '-'))
    finally:
        _, now = w.get_json('/api/config/network')
        if now != home:
            restart_with(home)
            follow()
            _, now = w.get_json('/api/config/network')
        check(now == home, 'network is exactly as before the bench')

    print('bench: %s' % ('ok' if not failures else 'FAILED (%d)' % len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
