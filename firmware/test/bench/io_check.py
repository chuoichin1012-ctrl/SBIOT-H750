#!/usr/bin/env python3
"""Plan 07b bench check: the IO (spec §6.1, §6.6, §11.4).

    python3 stm32/firmware/test/bench/io_check.py [adcbase|dido|reload|adcpoint MA|adccal|adc ...]

Needs the board running this build's app, its LAN at 192.168.0.7 (factory login), UART7 on the bench
console. Cases:
- adcbase: the ADC3 registers (BOOST 01, OVSR 15, OVSS 4, 64.5 cycles), 10 interrupts a second (no
  interrupt per sample), and an injected fault (bench J): nothing published during the recovery, and a
  value again within 300 ms. Runs on any board (no 4-20 mA source needed).
- dido: each DO's relay contact (COM-NO) wired to one DI, through a 12-24 V supply (which DI is found
  first: DO1 may drive DI2). Commands, the DI
  debounce, diInvert, mode 1 (pulse 2 s), mode 2 (flip 1 s), keepOutputs over a restart, and a timer.
- reload: the Reload button pressed by hand when asked (1 s, 5 s, 20 s, and held through a restart).
- adcpoint MA: the source set to MA mA (AI1 and AI2 in series): prints the window means, the µA, and
  their spread over 60 s; adccal: writes the record from the 4 and 20 mA points saved by adcpoint;
  adc: after adccal and a restart, 4/12/20 mA within ±0.05 mA, the spread, and the latency.
Prints one line per check and "bench: ok"; exits 1 on any failure.
"""
import json
import os
import re
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402
import web_check as w  # noqa: E402

failures = []
POINTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.adc_points.json')


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def console(sends, seconds):
    out = []
    for t, s in sends:
        out.append((t, s[0]))
        if len(s) > 1:
            out.append((t + 0.1, s[1:] + '\r'))
    return board.listen(seconds, sends=out)


A_RE = re.compile(r'app: a mean ([\d.]+) ([\d.]+) ua (\d+) (\d+) ok (\d) (\d) cal (\d) windows (\d+) late (\d+) irqs (\d+) '
                  r'errors (\d+) recoveries (\d+) cr (\w+) cfgr (\w+) cfgr2 (\w+) smpr1 (\w+)')


def parse_a(line):
    m = A_RE.search(line)
    if not m:
        return None
    g = m.groups()
    return {'mean': [float(g[0]), float(g[1])], 'ua': [int(g[2]), int(g[3])], 'ok': [g[4] == '1', g[5] == '1'],
            'cal': g[6] == '1', 'windows': int(g[7]), 'late': int(g[8]), 'irqs': int(g[9]), 'errors': int(g[10]),
            'recoveries': int(g[11]), 'cr': int(g[12], 16), 'cfgr': int(g[13], 16), 'cfgr2': int(g[14], 16),
            'smpr1': int(g[15], 16)}


def ai_state(times):
    """'a' at each of `times` (s): [(ms, state)]."""
    lines = console([(t, 'a') for t in times], max(times) + 1)
    return [(ms, parse_a(l)) for ms, l in lines if parse_a(l)]


def case_adcbase():
    st = ai_state([0.3])
    check(bool(st), 'the AI state answers on the console')
    if not st:
        return
    s = st[0][1]
    boost = (s['cr'] >> 8) & 3
    ovsr, ovss, rovse = (s['cfgr2'] >> 16) & 0x3FF, (s['cfgr2'] >> 5) & 0xF, s['cfgr2'] & 1
    smp0, smp1 = s['smpr1'] & 7, (s['smpr1'] >> 3) & 7
    extsel, exten, dmngt, res = (s['cfgr'] >> 5) & 0x1F, (s['cfgr'] >> 10) & 3, s['cfgr'] & 3, (s['cfgr'] >> 2) & 7
    check(boost == 1 and rovse == 1 and ovsr == 15 and ovss == 4 and smp0 == 5 and smp1 == 5 and extsel == 13 and exten == 1
          and dmngt == 3 and res == 0,
          'ADC3: BOOST %d, ROVSE %d OVSR %d OVSS %d, SMP %d/%d (64.5), EXTSEL %d (TIM6) EXTEN %d, DMNGT %d, RES %d'
          % (boost, rovse, ovsr, ovss, smp0, smp1, extsel, exten, dmngt, res))
    a = ai_state([0.3, 10.3])
    if len(a) == 2:
        (t0, s0), (t1, s1) = a
        rate = (s1['irqs'] - s0['irqs']) * 1000.0 / (t1 - t0)
        wins = (s1['windows'] - s0['windows']) * 1000.0 / (t1 - t0)
        check(rate <= 10.2 and wins >= 9.8 and s1['late'] == s0['late'] and s1['errors'] == s0['errors'],
              'ADC interrupts %.2f a second (no interrupt per sample), %.2f windows a second, late +%d, errors +%d'
              % (rate, wins, s1['late'] - s0['late'], s1['errors'] - s0['errors']))
    # An injected fault: nothing published while it recovers, a value again within 300 ms.
    times = [0.3 + 0.05 * k for k in range(20)]
    lines = console([(0.25, 'J')] + [(t, 'a') for t in times], 2)
    jm = next((ms for ms, l in lines if 'J ADC fault injected' in l), None)
    states = [(ms, parse_a(l)) for ms, l in lines if parse_a(l)]
    after = [(ms, s) for ms, s in states if jm is not None and ms > jm]
    first_off = next((ms for ms, s in after if not s['ok'][0]), None)
    back = next((ms for ms, s in after if first_off and ms > first_off and s['ok'][0] and s['ok'][1]), None)
    rec = states[-1][1]['recoveries'] if states else 0
    check(jm is not None and first_off is not None and back is not None and back - jm <= 300 + 60 and rec >= 1,
          'an injected fault: unpublished at +%s ms, published again at +%s ms (300 ms, plus the console poll), '
          'recoveries %d' % (first_off - jm if first_off else '-', back - jm if back else '-', rec))
    on_again = [s for ms, s in after if back and ms >= back]
    check(all(s['ok'][0] and s['ok'][1] for s in on_again), 'and stays published')


def io():
    s, d = w.get_json('/api/io')
    return d if s == 200 else {}


def set_do(i, on):
    s, _, _ = w.req('POST', '/api/io', body=json.dumps({'do': i, 'value': on}).encode())
    return s == 200


def setup_io(**fields):
    _, cfg = w.get_json('/api/config/io')
    cfg.update(fields)
    w.req('POST', '/api/config/io', body=json.dumps(cfg).encode())
    ok = w.restart()
    time.sleep(1)
    return ok


def trace(during, seconds):
    """The board's own record of the outputs (bench D) while during() drives them: [(ms, mask)].
    HTTP is no clock: its answers stall now and then, and right after a boot it skews."""
    lines = board.listen(seconds, sends=[(0.3, 'D'), (seconds - 0.5, 'D')], during=during)
    out = []
    for _, l in lines:
        m = re.search(r'outputs 0x(\w+) at (\d+) ms', l)
        if m:
            out.append((int(m.group(2)), int(m.group(1), 16)))
    return out


def wait_di(i, want, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if io().get('di', [None, None])[i] == want:
            return True
        time.sleep(0.05)
    return False


def find_pairs():
    """{do: di} for the DO whose relay drives a DI."""
    pairs = {}
    for i in (0, 1):
        set_do(i, True)
        time.sleep(0.4)
        on = io().get('di', [False, False])
        set_do(i, False)
        time.sleep(0.4)
        off = io().get('di', [False, False])
        hit = [k for k in (0, 1) if on[k] and not off[k]]
        if len(hit) == 1:
            pairs[i] = hit[0]
    return pairs


def case_dido():
    check(setup_io(keepOutputs=0, do0Mode=0, do1Mode=0, diDebounce=50, diInvert=0), 'set up io: follow, debounce 50 ms')
    pairs = find_pairs()
    check(bool(pairs), 'wiring: %s' % (', '.join('DO%d -> DI%d' % (d + 1, i + 1) for d, i in pairs.items()) or 'no DO drives a DI'))
    if not pairs:
        return
    for d, i in pairs.items():
        set_do(d, True)
        on = wait_di(i, True, 0.5)
        set_do(d, False)
        off = wait_di(i, False, 0.5)
        check(on and off, 'DO%d on: DI%d active; off: inactive (within 500 ms)' % (d + 1, i + 1))
    pulse_do, pulse_di = next(iter(pairs.items()))
    flip_do, flip_di = list(pairs.items())[-1]
    # The DI filter (diDebounce, "Debounce time" in IO function): a new level must hold that long.
    check(setup_io(diDebounce=1000), 'set up diDebounce 1000 ms')
    d0, i0 = next(iter(pairs.items()))
    t0 = time.time()
    set_do(d0, True)
    reads = []
    while time.time() - t0 < 2.5:
        v = io().get('di', [None, None])[i0]
        reads.append((time.time() - t0, v))
        time.sleep(0.05)
    early = [v for t, v in reads if t < 0.8]
    first_on = next((t for t, v in reads if v), None)
    set_do(d0, False)
    time.sleep(2)
    t0 = time.time()
    set_do(d0, True)
    time.sleep(0.5)
    set_do(d0, False)
    blip = [io().get('di', [None, None])[i0] for _ in range(20) if not time.sleep(0.1)]
    check(early and not any(early) and first_on is not None and 0.9 <= first_on <= 2.0 and not any(blip),
          'diDebounce 1000: DI%d active %.2f s after DO%d on (none before 0.8 s); a 0.5 s pulse never shows'
          % (i0 + 1, first_on or -1, d0 + 1))
    check(setup_io(diInvert=1, diDebounce=50), 'set up diInvert 1')
    set_do(pulse_do, False)
    check(wait_di(pulse_di, True, 0.5), 'diInvert: DO%d off reads DI%d active' % (pulse_do + 1, pulse_di + 1))
    modes = {'diInvert': 0, 'do%dMode' % pulse_do: 1, 'do%dFlipDelay' % pulse_do: 2}
    check(setup_io(**modes), 'set up DO%d pulse 2 s' % (pulse_do + 1))
    def pulse():
        time.sleep(1)
        set_do(pulse_do, True)
    tr = trace(pulse, 5)
    bit = 1 << pulse_do
    ons = [t for t, m in tr if m & bit]
    offs = [t for t, m in tr if not m & bit and ons and t > ons[0]]
    dt = offs[0] - ons[0] if ons and offs else -1
    check(1990 <= dt <= 2020 and not wait_di(pulse_di, True, 0.3), 'mode 1: DO%d on, then off %d ms later (2000), DI%d off'
          % (pulse_do + 1, dt, pulse_di + 1))
    check(setup_io(**{'do%dMode' % pulse_do: 0, 'do%dMode' % flip_do: 2, 'do%dFlipDelay' % flip_do: 1}),
          'set up DO%d flip 1 s' % (flip_do + 1))

    def flip():
        time.sleep(1)
        set_do(flip_do, True)
        time.sleep(6)
        set_do(flip_do, False)
    tr = trace(flip, 9)
    bit = 1 << flip_do
    edges = [t for (t, m), (_, p) in zip(tr[1:], tr) if (m ^ p) & bit] if len(tr) > 1 else []
    gaps = [b - a for a, b in zip(edges, edges[1:])][:-1]  # the last edge is the command off
    stays = wait_di(flip_di, False, 1.5) and not wait_di(flip_di, True, 1.5)
    check(len(gaps) >= 4 and all(990 <= g <= 1015 for g in gaps) and stays,
          'mode 2: DO%d toggles every %s ms (1000), and DI%d stays off after an off' % (flip_do + 1, gaps, flip_di + 1))
    d, i = pulse_do, pulse_di
    check(setup_io(do0Mode=0, do1Mode=0, keepOutputs=1), 'set up keepOutputs 1')
    set_do(d, True)
    time.sleep(0.5)  # the reload task saves within 50 ms
    w.restart()
    time.sleep(1)
    check(io().get('do', [False, False])[d] and wait_di(i, True, 0.5), 'keepOutputs 1: DO%d still on after a restart' % (d + 1))
    check(setup_io(keepOutputs=0), 'set up keepOutputs 0 (DO%d was on)' % (d + 1))
    check(not io().get('do', [True, True])[d] and wait_di(i, False, 0.5),
          'keepOutputs 0: DO%d off after the restart (the bootloader latched zeros)' % (d + 1))
    # A timer, 30 s ahead (time enough for the restart that applies it); the clock set after the restart
    # (no RTC; the bench LAN has no NTP).
    hhmm = time.strftime('%H:%M:%S', time.localtime(time.time() + 30))
    check(setup_io(timer0Enabled=1, timer0Time=hhmm, timer0Action=1, timer0Output=d, timer0OutputAction=1),
          'set up timer 0: close DO%d at %s' % (d + 1, hhmm))
    w.req('POST', '/api/time', body=json.dumps({'time': time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())}).encode())
    fired = wait_di(i, True, 40)
    check(fired, 'timer 0 closed DO%d at %s (seen on DI%d)' % (d + 1, hhmm, i + 1))
    set_do(d, False)
    setup_io(timer0Enabled=0, keepOutputs=0)


def case_reload():
    print('reload: watch the prompts; press and release the Reload button as asked')
    _, sysc = w.get_json('/api/config/system')
    for hold, expect in ((1, False), (20, False), (5, True)):
        sysc['hostname'] = 'reload-mark'
        w.req('POST', '/api/config/system', body=json.dumps(sysc).encode())
        input('hold Reload %d s, release, then press Enter: ' % hold)
        time.sleep(3)
        w.wait_up(30)
        _, now = w.get_json('/api/config/system')
        reset = now.get('hostname') != 'reload-mark'
        check(reset == expect, 'Reload %d s: %s' % (hold, 'factory settings' if reset else 'nothing'))
        sysc = now
    sysc['hostname'] = 'reload-mark'
    w.req('POST', '/api/config/system', body=json.dumps(sysc).encode())
    input('hold Reload, then press Enter (the board restarts); keep holding 6 s more, release, press Enter: ')
    w.req('POST', '/api/restart')
    input('released? press Enter: ')
    w.wait_up(30)
    _, now = w.get_json('/api/config/system')
    check(now.get('hostname') == 'reload-mark', 'held through a restart: nothing until released')


def window_stats(seconds):
    times = [0.3 + 0.5 * k for k in range(int(seconds * 2))]
    st = [s for _, s in ai_state(times)]
    return st


def case_adcpoint(ma):
    st = window_stats(60)
    pts = json.load(open(POINTS)) if os.path.exists(POINTS) else {}
    means = [statistics.mean(s['mean'][ch] for s in st) for ch in (0, 1)]
    sd = [statistics.pstdev(s['ua'][ch] / 1000.0 for s in st) for ch in (0, 1)]
    pts[str(ma)] = means
    json.dump(pts, open(POINTS, 'w'))
    print('adcpoint %s mA: means %.1f %.1f, mA %.4f %.4f, spread %.4f %.4f mA over %d windows'
          % (ma, means[0], means[1], st[-1]['ua'][0] / 1000.0, st[-1]['ua'][1] / 1000.0, sd[0], sd[1], len(st)))


def case_adccheck(ma):
    st = window_stats(30)
    for ch in (0, 1):
        vals = [x['ua'][ch] / 1000.0 for x in st if x['ok'][ch]]
        mean = statistics.mean(vals) if vals else -1
        sd = statistics.pstdev(vals) if vals else -1
        check(len(vals) == len(st) and abs(mean - float(ma)) <= 0.05 and sd <= 0.005 and st[-1]['cal'],
              'AI%d at %s mA: %.4f mA (error %+.4f, limit 0.05), spread %.4f mA (limit 0.005), calibrated'
              % (ch + 1, ma, mean, mean - float(ma), sd))


def case_adccal():
    pts = json.load(open(POINTS))
    k = 3.0 / (65535 * 0.499 * 0.120)  # mA per code, nominal
    coef = []
    for ch in (0, 1):
        i4, i20 = pts['4'][ch] * k, pts['20'][ch] * k
        g = 16.0 / (i20 - i4)
        coef += [g, 4.0 - g * i4]
    lines = console([(0.3, 'A %.6f %.6f %.6f %.6f' % tuple(coef))], 2)
    ok = any('copy A ok, copy B ok' in l for _, l in lines)
    check(ok, 'the factory record written: gain %.5f offset %.4f, gain %.5f offset %.4f' % tuple(coef))
    w.restart()
    time.sleep(1)
    _, s = w.get_json('/api/status')
    check(s.get('aiCalibrated') is True, 'aiCalibrated after the restart')


def main():
    if not w.wait_up(5):
        print('the board does not answer on %s' % w.HOST)
        return 1
    args = sys.argv[1:] or ['adcbase']
    while args:
        a = args.pop(0)
        if a == 'adcpoint':
            case_adcpoint(args.pop(0))
        elif a == 'adccheck':
            case_adccheck(args.pop(0))
        else:
            {'adcbase': case_adcbase, 'dido': case_dido, 'reload': case_reload, 'adccal': case_adccal}[a]()
    print('bench: %s' % ('ok' if not failures else 'FAILED (%d)' % len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
