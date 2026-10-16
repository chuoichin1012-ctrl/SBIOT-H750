#!/usr/bin/env python3
"""Plan 10 bench: the EC600G-EU (spec §7.2, §12.2). Needs the module and a data SIM; the Internet over LTE.

    python3 stm32/firmware/test/bench/lte_check.py [case ...]

Cases:
- identity: IMEI and ICCID in /api/status, and the IMEI as UART1's registration packet (TCP client to the PC);
- power: a 2.5 s PWRKEY on a running module leaves it on; a restart gets POWERED DOWN, then the full power-on;
- mux: CMUX running, the DLC2 signal, a DISC on DLC2 recovered;
- priority: priority 2 routes over LTE (NTP, MQTT to a public broker); priority 1 with the bench LAN, which
  has no Internet: the Internet check moves the route to LTE; the PHY down 30 s and back;
- load: MQTT over LTE both ways for 5 minutes while DLC2 polls: overrun 0, bad FCS 0, the data through;
- cfun: no PPP address for 2 minutes (PPP's input dropped by the bench): CFUN, then RECOVER;
- fallback: AT+CMUX refused (forced by the console 'j'): PPP on the plain UART, and a restart leaves data mode
  (+++, ATH) before QPOWD;
- p0: priority 0 powers down a module that answers;
- backoff: no PPP address over CMUX, with the waits divided by 10 (console 'b'): CFUN after 12 s, then 30 s,
  then a soft power cycle after 60 s (POWERED DOWN, then PWRKEY with no RESET_N between); an address resets it;
- backoff_plain: the same on the plain UART (the fallback forced): RESET_N twice, then the soft power cycle;
- backoff_nopd: the power cycle with POWERED DOWN disregarded (console 'd'): UNKNOWN, a RECOVER, PWRKEY only
  after RESET_N and AT have failed.
Ends with priority 1, MQTT off. Prints one line per check and "bench: ok".
"""
import json
import os
import re
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402
import bridge_check as bc  # noqa: E402
import mqtt_check as mc  # noqa: E402
import serial_check as sc  # noqa: E402
import web_check as w  # noqa: E402

check = bc.check
BROKER = 'broker.emqx.io'


def status():
    _, st = w.get_json('/api/status')
    return st or {}


def cell():
    return status().get('cell', {})


def log_text():
    s, _, d = w.req('GET', '/api/log')
    return d.decode(errors='replace').split('# Since the last start', 1)[-1] if s == 200 else ''


def wait(cond, secs, step=2.0):
    end = time.time() + secs
    while time.time() < end:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return cond()


def network(**kw):
    _, n = w.get_json('/api/config/network')
    n.update(kw)
    return w.req('POST', '/api/config/network', body=json.dumps(n, separators=(',', ':')).encode())[0] == 200


def restart():
    w.req('POST', '/api/restart', body=b'')
    time.sleep(15)
    return w.wait_up(90)


def console(sends, seconds):
    """UART7 console lines; sends: [(t, text)] (text with its CR when the command takes a line)."""
    return [l for _, l in board.listen(seconds, sends=sends)]


def ppp_up(secs=120):
    return wait(lambda: cell().get('state') == 'connected', secs)


# ---- cases ---------------------------------------------------------------------------------------------------

def case_identity():
    check(network(priority=1) and restart() and ppp_up(), 'set up: priority 1, PPP up')
    st = status()
    imei, iccid = st['system'].get('imei', ''), st['cell'].get('iccid', '')
    check(re.fullmatch(r'\d{15}', imei) and re.fullmatch(r'[0-9A-Fa-f]{19,20}', iccid), 'IMEI %s, ICCID %s' % (imei, iccid))
    tcp = bc.peer('tcpserver', port=9000, secs=60)  # the restart (with the modem's QPOWD) and the boot inside
    # a heartbeat, not the registration: the registration goes once at the connect, before the modem has answered
    check(bc.setup(1, **bc.base(workMode=1, netHeartbeat=True, netHeartbeatContent=1, netHeartbeatHex=0, netHeartbeatInterval=2)),
          'set up UART1: TCP client to the PC, an IMEI heartbeat every 2 s')
    data = bc.data_of(bc.wait_peer(tcp, 1))
    check(imei.encode() in data, 'the heartbeat carries the IMEI once the modem answered (%r)' % data[-20:])


def case_power():
    check(network(priority=1) and restart() and ppp_up(), 'set up: PPP up')
    ip = cell().get('ip')
    lines = console([(1, 'K'), (1.1, '2500\r')], 25)
    c = cell()
    check(any('PWRKEY 2500' in l for l in lines) and c.get('modem') == 'ON' and c.get('state') == 'connected' and c.get('ip') == ip,
          'a 2.5 s PWRKEY on a running module: it stays on, PPP kept (%s %s %s)' % (c.get('modem'), c.get('state'), c.get('ip')))
    out = {}
    t = threading.Thread(target=lambda: out.update(lines=console([], 70)))
    t.start()
    time.sleep(2)
    w.req('POST', '/api/restart', body=b'')
    t.join()
    l = out.get('lines', [])
    down = next((i for i, x in enumerate(l) if 'POWERED DOWN' in x), -1)
    rst = next((i for i, x in enumerate(l) if 'sys: restarting' in x), -1)
    check(down >= 0 and rst > down, 'a restart: AT+QPOWD=1, POWERED DOWN, then the reset')
    seq = [x for x in l[rst:] if re.search(r'lte: (RESET_N|PWRKEY)', x)]
    check(len(seq) == 2 and 'RESET_N' in seq[0] and 'PWRKEY 2500' in seq[1], 'the next boot: RESET_N, then PWRKEY 2.5 s (%s)' % seq)
    check(w.wait_up(60) and ppp_up(), 'and PPP up again')


def case_mux():
    check(network(priority=1) and restart() and ppp_up(), 'set up: PPP up')
    c = cell()
    check(c.get('mux') == 'RUNNING' and 0 <= int(c.get('signal', 99)) < 99, 'CMUX running, the signal from DLC2 (%s)' % c.get('signal'))
    t0 = time.time()
    console([(1, 'Q')], 3)
    back = ppp_up(90)
    check(back and 'recovering: CLD, RESET_N' in log_text(), 'DISC on DLC2: recovered, PPP up again in %d s' % (time.time() - t0))


def case_priority():
    check(mc.post('mqtt', mc.mqtt_cfg(host=BROKER, port=1883)), 'MQTT to %s' % BROKER)
    check(network(priority=2) and restart() and ppp_up(), 'priority 2: PPP up')
    ok = wait(lambda: status().get('system', {}).get('activeNet') == 'LTE' and status()['mqtt']['state'] == 'CONNECTED'
              and status()['system']['clockSet'], 90)
    check(ok, 'priority 2: the route over LTE, MQTT connected and the clock set by NTP over it')
    check(network(priority=1) and restart() and ppp_up(), 'priority 1: PPP up')
    st = wait(lambda: status() if status()['system']['activeNet'] == 'LTE' else None, 180, 5)
    check(st and st['eth']['internet'] is False and st['cell']['internet'] is True,
          'priority 1, the bench LAN without Internet: the Internet check moves the route to LTE (eth %s, cell %s)'
          % (st and st['eth']['internet'], st and st['cell']['internet']))
    check(wait(lambda: status()['mqtt']['state'] == 'CONNECTED', 60), 'and MQTT connects over LTE')
    lines = console([(1, 'H')], 45)
    check(any('link down' in l for l in lines) and any('link up' in l for l in lines), 'the PHY down 30 s and up again')
    check(wait(lambda: status()['system']['activeNet'] == 'LTE', 150, 5), 'the route back on LTE (Ethernet still has no Internet)')
    mc.post('mqtt', mc.mqtt_cfg(enabled=0, host=''))


def case_load():
    """The down direction only: this board has no RS485 loop between UART1 and UART2 (the up direction needs one)."""
    imei = status()['system'].get('imei', 'x')
    down_t = 'sbiot/%s/down' % imei
    _, p1 = w.get_json('/api/config/port1')
    p1.update(bc.base(baudRate=115200))
    ok = mc.configure(mc.mqtt_cfg(host=BROKER, port=1883), subs=[mc.topic(down_t)], port1=p1)
    check(ok and network(priority=2) and restart() and ppp_up() and wait(lambda: status()['mqtt']['state'] == 'CONNECTED', 60),
          'set up: priority 2, MQTT over LTE, UART1 takes %s' % down_t)
    c0, tx0 = cell()['counters'], sc.counters(1).get('tx', 0)
    secs, size, every = 300, 1000, 0.25  # about 4 KB/s down, with DLC2 polling the signal every 20 s
    pub = subprocess.Popen(['mosquitto_pub', '-h', BROKER, '-t', down_t, '-l'], stdin=subprocess.PIPE, text=True)
    signals = set()
    t0, sent = time.time(), 0
    while time.time() - t0 < secs:
        pub.stdin.write('d' * size + '\n')
        pub.stdin.flush()
        sent += size
        time.sleep(every)
        if int(time.time() - t0) % 20 == 0:
            signals.add((cell().get('signal'), cell().get('netType')))
    pub.stdin.close()
    pub.wait(30)
    time.sleep(10)
    c1, tx1 = cell()['counters'], sc.counters(1).get('tx', 0)
    check(c1['overrun'] == c0['overrun'] and c1['badFcs'] == c0['badFcs'], 'UART3 under load: overrun +%d, bad FCS +%d'
          % (c1['overrun'] - c0['overrun'], c1['badFcs'] - c0['badFcs']))
    check(tx1 - tx0 >= 0.9 * sent, 'down: %d of %d bytes over LTE reached UART1' % (tx1 - tx0, sent))
    check(signals and all(x[0] is not None and x[0] != 99 for x in signals), 'the signal read during the load %s' % sorted(signals))
    print('     counters after: %s, stack cmux_rx %s B, lte %s B, heap lowest %s'
          % (c1, cell().get('stackCmux'), cell().get('stackLte'), status()['system']['heapMin']))
    mc.post('mqtt', mc.mqtt_cfg(enabled=0, host=''))


def case_cfun():
    check(network(priority=1) and restart(), 'set up: priority 1')
    lines = console([(1, 'q')], 175)
    lines += console([(1, 'q')], 60)
    text = '\n'.join(lines)
    check('AT+CFUN=1,1' in text and 'recovering' in text.split('AT+CFUN=1,1', 1)[-1], 'no address for 2 minutes: CFUN, then RECOVER')
    check(ppp_up(120), 'PPP up again once its input passes')


def case_fallback():
    """AT+CMUX refused (forced by the bench): PPP on the plain UART; then a restart leaves data mode for QPOWD."""
    check(network(priority=2) and restart() and ppp_up(), 'set up: priority 2, PPP up over CMUX')
    console([(1, 'j'), (2, 'Q')], 4)  # the fallback forced, then a recovery reopens
    back = ppp_up(120)
    c = cell()
    check(back and c.get('mux') == 'PLAIN' and 'AT+CMUX refused' in log_text(), 'AT+CMUX refused: PPP up on the plain UART (%s)' % c.get('mux'))
    check(wait(lambda: status()['system']['activeNet'] == 'LTE', 30), 'the route over LTE')
    out = {}
    t = threading.Thread(target=lambda: out.update(lines=console([], 40)))
    t.start()
    time.sleep(2)
    w.req('POST', '/api/restart', body=b'')
    t.join()
    l = '\n'.join(out.get('lines', []))
    check('POWERED DOWN' in l and 'no OK to ATH' not in l, 'a restart in data mode: +++, ATH, then POWERED DOWN')
    w.wait_up(90)


def case_p0():
    check(network(priority=0) and restart(), 'set up: priority 0')
    lines = console([(1, 'K'), (1.1, '2500\r')], 15)  # the module on by hand
    out = {}
    t = threading.Thread(target=lambda: out.update(lines=console([], 30)))
    t.start()
    time.sleep(2)
    w.req('POST', '/api/restart', body=b'')
    t.join()
    l = '\n'.join(out.get('lines', []))
    check('Ethernet only' in l and 'POWERED DOWN' in l and 'PWRKEY' not in l.split('Ethernet only', 1)[-1],
          'priority 0: a module that answers is powered down, PWRKEY never pressed')
    w.wait_up(60)
    c = cell()
    check(c.get('modem') == 'OFF' and c.get('state') == 'disconnected', 'modem OFF, cell disconnected (%s)' % c.get('modem'))


def found(lines, pat, start=0):
    """The index of the first line from start matching pat, -1 if none."""
    return next((i for i in range(start, len(lines)) if re.search(pat, lines[i])), -1)


def cmux_attempts(lines):
    """[(attempt, limit s, action, line index)] of the CMUX path's backoff."""
    out = []
    for i, l in enumerate(lines):
        m = re.search(r'no PPP address for (\d+) s \(attempt (\d+)\): (.*?)\s*$', l)
        if m:
            out.append((int(m.group(2)), int(m.group(1)), m.group(3), i))
    return out


def power_cycled(lines, at):
    """After line at: POWERED DOWN, then PWRKEY 2.5 s with no RESET_N between."""
    down = found(lines, r'lte: POWERED DOWN', at)
    key = found(lines, r'lte: PWRKEY 2500', max(down, at))
    rst = found(lines, r'lte: RESET_N', max(down, at))
    return down > at and key > down and (rst < 0 or rst > key)


def backoff_reset():
    """'q' passes PPP's input again: PPP up, and the attempts back to 0."""
    console([(1, 'q')], 3)
    check(ppp_up(240) and wait(lambda: cell().get('recoveries') == 0, 40),
          'an address: PPP up, recoveries %s' % cell().get('recoveries'))


def case_backoff():
    check(network(priority=1) and restart(), 'set up: priority 1')
    lines = console([(1, 'b'), (1.5, 'q')], 270)  # the waits / 10, PPP's input dropped
    a = cmux_attempts(lines)
    got = [(n, l, act) for n, l, act, _ in a]
    check(got[:3] == [(1, 12, 'AT+CFUN=1,1'), (2, 30, 'AT+CFUN=1,1'), (3, 60, 'a soft power cycle')],
          'no address: CFUN after 12 s, CFUN after 30 s, then a soft power cycle after 60 s (%s)' % got)
    check(len(a) >= 3 and power_cycled(lines, a[2][3]), 'the power cycle: POWERED DOWN, then PWRKEY 2.5 s, no RESET_N')
    check(cell().get('recoveries') == 3, 'cell.recoveries 3 (%s)' % cell().get('recoveries'))
    backoff_reset()


def case_backoff_plain():
    check(network(priority=1) and restart() and ppp_up(), 'set up: PPP up over CMUX')
    # the waits / 10, PPP's input dropped, the fallback forced, then a DISC on DLC2 reopens on the plain UART
    lines = console([(1, 'b'), (1.5, 'q'), (2, 'j'), (3, 'Q')], 240)
    a = [(int(m.group(1)), m.group(2), i) for i, l in enumerate(lines)
         for m in [re.search(r'plain UART, attempt (\d+): (.*?)\s*$', l)] if m]
    lim = [int(m.group(1)) for l in lines for m in [re.search(r'plain UART: no address within (\d+) s', l)] if m]
    got = [(n, act) for n, act, _ in a]
    check(got[:3] == [(1, 'RESET_N'), (2, 'RESET_N'), (3, 'a soft power cycle')] and lim[:3] == [12, 30, 60],
          'the plain UART: RESET_N after 12 s, after 30 s, then a soft power cycle after 60 s (%s, %s)' % (got, lim))
    check(len(a) >= 3 and power_cycled(lines, a[2][2]) and not any('no OK to ATH' in l for l in lines),
          'the power cycle: out of data mode, POWERED DOWN, then PWRKEY 2.5 s, no RESET_N')
    backoff_reset()
    check(cell().get('mux') == 'PLAIN', 'PPP up on the plain UART (%s)' % cell().get('mux'))


def case_backoff_nopd():
    check(network(priority=1) and restart(), 'set up: priority 1')
    lines = console([(1, 'b'), (1.5, 'q'), (2, 'd')], 290)  # POWERED DOWN disregarded
    a = cmux_attempts(lines)
    check(len(a) >= 3 and a[2][:3] == (3, 60, 'a soft power cycle'), 'the third attempt: a soft power cycle (%s)' % a[:3])
    at = a[2][3] if len(a) >= 3 else len(lines)
    warn = found(lines, r'no POWERED DOWN within 60 s', at)
    rec = found(lines, r'recovering: CLD, RESET_N', max(warn, at))
    key = found(lines, r'lte: PWRKEY', max(warn, at))
    rsts = [i for i in range(max(rec, 0), key if key > 0 else len(lines)) if 'lte: RESET_N' in lines[i]] if rec > 0 else []
    check(warn > at and rec > warn and key > rec and len(rsts) >= 2,
          'no POWERED DOWN: UNKNOWN, a RECOVER, PWRKEY only after RESET_N and AT failed (RESET_N x%d)' % len(rsts))
    backoff_reset()


CASES = {'identity': case_identity, 'power': case_power, 'mux': case_mux, 'priority': case_priority, 'load': case_load,
         'cfun': case_cfun, 'fallback': case_fallback, 'p0': case_p0, 'backoff': case_backoff,
         'backoff_plain': case_backoff_plain, 'backoff_nopd': case_backoff_nopd}


def main():
    if not w.wait_up(10):
        print('the board does not answer')
        return 1
    try:
        for name in sys.argv[1:] or CASES:
            CASES[name]()
    finally:
        mc.post('mqtt', mc.mqtt_cfg(enabled=0, host=''))
        network(priority=1)
        restart()
    print('bench: %s' % ('ok' if not bc.failures else 'FAILED (%d)' % len(bc.failures)))
    return 1 if bc.failures else 0


if __name__ == '__main__':
    sys.exit(main())
