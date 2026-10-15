#!/usr/bin/env python3
"""Plan 06a bench check: Ethernet and lwIP on the board (spec §5.6, §6.1, §7.1).

    python3 stm32/firmware/test/bench/net_check.py [--dhcp]

Needs the board running this build's app (installed over UART7: board.upload), its Ethernet cabled to
the bench PC, whose port is fixed at 192.168.0.10/24. WSL2 runs in NAT mode, so the pings go through
Windows (ping.exe) and the flood runs from Windows Python. Cases:
1. the factory default (static 192.168.0.7): the link at 100 Mbit full duplex, pings, a 3000-byte ping
   (fragmented, reassembled by lwIP), the PHY found;
2. a static address from the config, after a restart: it answers, the old one does not, and the
   status shows the running values; then the factory default again;
3. the link dropped five times (the PHY powered down from the console): it comes back each time,
   pings answer, and the RX buffers in use return to the baseline;
4. a 20 s UDP flood: the watchdog task keeps its period, the console answers, the CPU still idles;
   afterwards pings answer within 2 s and the RX buffers return to the baseline;
5. --dhcp (only on a LAN with a DHCP server): a lease is taken and answers pings.
Prints one line per check, the measurements, and "bench: ok"; exits 1 on any failure.
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402

WIN_PY = '/mnt/c/Python311/python.exe'
FACTORY = ('{"ipMode":1,"ip":"192.168.0.7","netmask":"255.255.255.0","gateway":"192.168.0.1",'
           '"dnsMode":1,"dns1":"8.8.8.8","dns2":"1.1.1.1"}')
OTHER = ('{"ipMode":1,"ip":"192.168.0.77","netmask":"255.255.255.0","gateway":"192.168.0.1",'
         '"dnsMode":1,"dns1":"9.9.9.9","dns2":"1.0.0.1"}')
DHCP = '{"ipMode":0,"dnsMode":0}'
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


def restart_with(config):
    """Saves `network` ('N', then the line once the console listens: it polls every 10 ms, and its
    16-byte FIFO would overflow) and restarts; returns the boot's lines, up to the link at 100 Mbit (the bench
    PC first offers 10 Mbit only, then renegotiates: B2)."""
    saved = find(board.listen(2, sends=[(0.3, 'N'), (0.4, config + '\r')]), r'network saved: (-?\d+)')
    check(saved is not None and saved.group(1) == '0', 'network config saved (%s)' % (saved.group(1) if saved else '-'))
    return board.listen(14, during=board.hard_reset)


def main():
    dhcp = '--dhcp' in sys.argv[1:]

    # 1. The factory default
    lines = restart_with(FACTORY)
    phy = find(lines, r'net: PHY at (\d+), id ([0-9a-f]+)')
    check(phy is not None and phy.group(2) == '02430c54', 'IP101 found (%s)' % (phy.group(0) if phy else '-'))
    check(find(lines, r'net: static 192\.168\.0\.7 mask 255\.255\.255\.0 gw 192\.168\.0\.1') is not None, 'static 192.168.0.7 from the config')
    up = find(lines, r'net: link (up|now), 100 Mbit full duplex')
    check(up is not None, 'link up at 100 Mbit full duplex')
    n = ping('192.168.0.7', 20)
    check(n == 20, '20 pings, %d answered' % n)
    n = ping('192.168.0.7', 4, 3000)
    check(n == 4, 'a 3000-byte ping (fragmented): %d of 4' % n)
    lines, rx = status()
    base = int(rx.group(1)) if rx else -1
    print('     RX buffers in use at rest: %d' % base)

    # 2. Another static address, after a restart
    lines = restart_with(OTHER)
    check(ping('192.168.0.77', 4) == 4 and ping('192.168.0.7', 2, wait_ms=500) == 0, '.77 answers after the restart, .7 does not')
    lines, _ = status()
    check(find(lines, r'static 192\.168\.0\.77 mask 255\.255\.255\.0 gw 192\.168\.0\.1 dns 9\.9\.9\.9 1\.0\.0\.1') is not None,
          'the status shows the running values')
    restart_with(FACTORY)
    check(ping('192.168.0.7', 4) == 4, 'the factory default again')

    # 3. Link drops
    bad = 0
    for i in range(5):
        lines = board.listen(14, sends=[(0.3, 'L')])
        down = find(lines, r'net: link down')
        back = find(lines, r'net: link (up|now), 100 Mbit')
        ok = down is not None and back is not None and ping('192.168.0.7', 3) == 3
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
        '    s.sendto(b"x"*1000,("192.168.0.7",9));n+=1\n'
        'print(n)\n')], stdout=subprocess.PIPE, text=True)
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
        print('     %s datagrams in 20 s; CPU idle during the flood: %.1f %%' % (sent, frac * 100))
    tic = time.time()
    n = ping('192.168.0.7', 2)
    check(n >= 1 and time.time() - tic < 3, 'pings answer right after the flood (%d of 2)' % n)
    lines, rx = status()
    st = find(lines, r'rx_starved (\d+) tx_busy_drops (\d+)')
    check(rx is not None and int(rx.group(1)) == base, 'RX buffers back to %d after the flood (%s)' % (base, rx.group(1) if rx else '-'))
    print('     rx_starved %s, tx_busy_drops %s' % (st.group(1) if st else '-', st.group(2) if st else '-'))

    # 5. DHCP, on a LAN with a server only
    if dhcp:
        lines = restart_with(DHCP)
        time.sleep(4)
        lines, _ = status()
        m = find(lines, r'dhcp (\d+\.\d+\.\d+\.\d+)')
        ip = m.group(1) if m else ''
        check(bool(ip) and ip != '0.0.0.0' and ping(ip, 4) == 4, 'DHCP lease %s answers' % ip)
        restart_with(FACTORY)

    print('bench: %s' % ('ok' if not failures else 'FAILED (%d)' % len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
