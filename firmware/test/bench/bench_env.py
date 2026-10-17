"""The bench's addresses (Plan 11b): the board and the Windows host on the board's LAN, in one place.

    SBIOT_GW   the board (default: found by a FinderTool search from SBIOT_WIN, by its SN)
    SBIOT_WIN  Windows on the board's LAN (default 192.168.0.10: the bench PC's Ethernet, cabled to the board;
               the routed LAN of 2026-10-17 was the PC's Wi-Fi, 192.168.1.3)
    SBIOT_SN   the bench board's SN (default SB2610000001)

WSL is behind NAT: the board reaches Windows only, so the peers it dials run there (win_peer.py)."""
import ipaddress
import json
import os
import random
import subprocess
import sys

WIN = os.environ.get('SBIOT_WIN', '192.168.0.10')
SN = os.environ.get('SBIOT_SN', 'SB2610000001')
WIN_PY = '/mnt/c/Python311/python.exe'


def pick(lines, req_id, sn):
    """The `found` reply to our search `req_id` from the board `sn`, as a dict; None for anything else. Its address
    must be a usable unicast IPv4 (Codex: no broadcast, multicast, 0.0.0.0, leading zeros, odd digits)."""
    for line in lines:
        try:
            r = json.loads(line)
            ip = ipaddress.IPv4Address(r['eth']['ip'])
        except (ValueError, KeyError, TypeError, ipaddress.AddressValueError):
            continue
        try:
            net = ipaddress.IPv4Network('%s/%s' % (ip, r['eth'].get('netmask', '255.255.255.255')), strict=False)
        except ValueError:
            continue
        if (r.get('sbiot') == 'disc' and r.get('v') == 1 and r.get('op') == 'found' and r.get('id') == req_id
                and r.get('sn') == sn and not (ip.is_multicast or ip.is_unspecified or ip.is_loopback or ip.is_reserved)
                and (net.prefixlen >= 31 or ip not in (net.network_address, net.broadcast_address))):
            return r
    return None


def find_board(win, sn):
    """The board's running Ethernet address by FinderTool's search from Windows (spec 2026-10-17-lan-discovery-design.md)."""
    req_id = '%08x' % random.getrandbits(32)
    pkt = json.dumps({'sbiot': 'disc', 'v': 1, 'op': 'search', 'id': req_id})
    code = ('import socket,time\n'
            's=socket.socket(2,2);s.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1);s.bind((%r,0));s.settimeout(0.2)\n'
            's.sendto(%r.encode(),("255.255.255.255",48600));t=time.time()\n'
            'while time.time()-t<2:\n'
            '    try: print(s.recvfrom(2048)[0].decode())\n'
            '    except socket.timeout: pass\n') % (win, pkt)
    try:
        out = subprocess.run([WIN_PY, '-c', code], cwd='/mnt/c', capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    return pick(out.splitlines(), req_id, sn)  # the whole reply: eth.ip, boot, …


def resolve_gw():
    gw = os.environ.get('SBIOT_GW') or (find_board(WIN, SN) or {}).get('eth', {}).get('ip')
    if not gw:
        raise RuntimeError('the board %s did not answer a search from %s: set SBIOT_GW, or check the LAN' % (SN, WIN))
    return gw


GW = resolve_gw()


def set_board(ip):
    """The board moved (net_check, disc_check, a new lease): every client that copied the address follows."""
    global GW
    GW = ip
    if 'web_check' in sys.modules:
        sys.modules['web_check'].HOST = ip
    if 'bridge_check' in sys.modules:
        sys.modules['bridge_check'].GW = ip


def gates(label, path=None):
    """The §6.1/§11.4/§12.2 counters of this boot, read now (they reset at every restart, so a suite calls this
    right after its load and before it restarts the board; Codex): appended as one JSON line to `path`. The boot
    number comes from the FinderTool reply (`found.boot`; the status has none). Missing counters are an error."""
    import web_check as w
    found = find_board(WIN, SN)
    _, st = w.get_json('/api/status')
    sy, mem = (st or {}).get('system', {}), (st or {}).get('mem', {})
    row = {'label': label, 'boot': (found or {}).get('boot'), 'uptime': sy.get('uptime'),
           'iwdgMaxGapMs': sy.get('iwdgMaxGapMs'), 'wdiMaxGapMs': sy.get('wdiMaxGapMs'), 'hbMissing': sy.get('hbMissing'),
           'heapLargest': sy.get('heapLargest'), 'heap': mem.get('heap'), 'stackMin': mem.get('stackMin'),
           'uart': mem.get('uart'),
           'poolsFull': [p['name'] for p in mem.get('pools', []) if p.get('used') == p.get('size')]}
    missing = [k for k in ('boot', 'uptime', 'iwdgMaxGapMs', 'wdiMaxGapMs', 'hbMissing', 'heap', 'stackMin', 'uart')
               if row[k] is None]
    if missing:
        raise RuntimeError('gates %s: no %s' % (label, ', '.join(missing)))
    with open(path or os.environ.get('SBIOT_GATES', '/tmp/sbiot_gates.jsonl'), 'a') as f:
        f.write(json.dumps(row) + '\n')
    return row


def same_boot(before, after):
    """A workload qualifies only if the board did not restart under it (Codex): the two gate rows' boot numbers."""
    return before['boot'] == after['boot']


DROPPING_POOLS = ('console_log',)  # built to drop when full, counted (spec §12.3)


def judge(before, after):
    """What a workload broke, from its gate rows (spec §6.1, §11.4, §12.2): a restart under it, a watchdog gap,
    a missing heartbeat slot, an overrun, a full pool. Empty when it qualifies."""
    out = []
    if before.get('boot') != after.get('boot'):
        out.append('restarted under the load (boot %s -> %s)' % (before.get('boot'), after.get('boot')))
    if after.get('iwdgMaxGapMs') is None or after['iwdgMaxGapMs'] >= 400:
        out.append('iwdgMaxGapMs %s >= 400' % after.get('iwdgMaxGapMs'))
    if after.get('wdiMaxGapMs') is None or after['wdiMaxGapMs'] >= 600:
        out.append('wdiMaxGapMs %s >= 600' % after.get('wdiMaxGapMs'))
    if after.get('hbMissing'):
        out.append('hbMissing %s' % after['hbMissing'])
    for n, u in enumerate(after.get('uart') or [], 1):
        if u.get('overrun'):
            out.append('UART%d overrun %s' % (n, u['overrun']))
    full = [p for p in after.get('poolsFull', []) if p not in DROPPING_POOLS]
    if full:
        out.append('pool full: %s' % ', '.join(full))
    return out


def qualify(label, before, path=None):
    """After a workload, before any restart: (ok, row, why). Call `gates(label + ' before')` first (Codex)."""
    after = gates(label, path)
    problems = judge(before, after)
    return not problems, after, '; '.join(problems) or 'iwdg %s ms, wdi %s ms, boot %s' % (
        after['iwdgMaxGapMs'], after['wdiMaxGapMs'], after['boot'])


def check_peer():
    """True when the Windows firewall lets the peers in: a rule allowing C:\\Python311\\python.exe inbound whose
    profiles include the board's LAN's (read-only; the agent never changes the firewall). A connection from WSL
    cannot tell, since it reaches Windows through the WSL adapter, not the LAN's."""
    ps = ("$cat = (Get-NetConnectionProfile | Where-Object { (Get-NetIPAddress -InterfaceIndex $_.InterfaceIndex "
          "-AddressFamily IPv4 -ErrorAction SilentlyContinue).IPAddress -eq '%s' }).NetworkCategory; "
          "$ok = Get-NetFirewallApplicationFilter -Program 'C:\\Python311\\python.exe' -ErrorAction SilentlyContinue | "
          "Get-NetFirewallRule | Where-Object { $_.Enabled -eq 'True' -and $_.Direction -eq 'Inbound' -and "
          "$_.Action -eq 'Allow' -and ($_.Profile -eq 'Any' -or $_.Profile -match $cat) }; "
          "if ($ok) { 'ok' } else { 'blocked ' + $cat }") % WIN
    out = subprocess.run(['powershell.exe', '-NoProfile', '-Command', ps], cwd='/mnt/c', capture_output=True,
                         text=True, timeout=60).stdout.strip()
    return out == 'ok', out or 'PowerShell gave nothing'  # a hint, not proof (Codex): the peers' own checks decide
