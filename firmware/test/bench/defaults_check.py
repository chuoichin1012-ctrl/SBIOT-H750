#!/usr/bin/env python3
"""Config defaults on the board (the user, 2026-10-17): no field null or empty, sample data in every config.

    python3 stm32/firmware/test/bench/defaults_check.py

1. Every object config GETs with all its keys, whatever the stored copy lacks (an old body, an older firmware).
2. A factory reset, then every config: no null, no empty string (but the fields where empty has its own
   meaning), the sample lists, and everything off (MQTT, Edge, the sample topics and linkage).
Ends on the defaults: run it before benches that set their own configs. Prints one line per check, "bench: ok".
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import web_check as w  # noqa: E402

check = w.check
OBJECTS = ['network', 'port1', 'port2', 'mqtt', 'system', 'io', 'edge', 'edgeReport']
LISTS = ['mqttPublish', 'mqttSubscribe', 'edgeDevices', 'edgeLinkage']
# empty on purpose: no conversion; a channel of IO devices only (spec §9.3)
EMPTY_OK = {'formula', 'ioPoint'}


def empties(doc, path=''):
    """The paths of null or empty values in doc."""
    out = []
    items = doc.items() if isinstance(doc, dict) else enumerate(doc) if isinstance(doc, list) else []
    for k, v in items:
        p = '%s.%s' % (path, k) if path else str(k)
        if v is None or (v == '' and k not in EMPTY_OK):
            out.append(p)
        elif isinstance(v, (dict, list)):
            out += empties(v, p)
    return out


def get(name):
    s, d = w.get_json('/api/config/' + name)
    return d if s == 200 else None


def main():
    if not w.wait_up(10):
        print('the board does not answer')
        return 1
    net = get('network') or {}
    check(all(k in net for k in ('apn', 'apnAuth', 'cellDnsMode', 'cellDns1', 'cellDns2')),
          'network as stored: the keys it lacks come from the defaults (apnAuth %r)' % net.get('apnAuth'))

    s, _, _ = w.req('POST', '/api/factory-reset', body=b'')
    check(s == 200 and w.wait_up(90), 'factory reset: the gateway restarts into the defaults')
    docs = {n: get(n) for n in OBJECTS + LISTS}
    for n in OBJECTS + LISTS:
        d = docs[n]
        bad = empties(d) if d is not None else ['(no answer)']
        check(not bad, '%s: no null or empty field %s' % (n, bad if bad else ''))
    for n in LISTS:
        check(isinstance(docs[n], list) and len(docs[n]) >= 1, '%s: a sample (%d)' % (n, len(docs[n] or [])))
    m, dev, ev = docs['mqtt'] or {}, (docs['edgeDevices'] or [{}])[0], (docs['edgeLinkage'] or [{}])[0]
    check(m.get('host') == 'broker.emqx.io' and m.get('clientId') == 'SBIOT-GW' and m.get('willTopic') == 'sbiot/gw/status',
          'mqtt: broker.emqx.io, SBIOT-GW, last will sbiot/gw/status')
    check([x.get('name') for x in dev.get('nodes', [])] == ['Temperature', 'Humidity'] and ev.get('point') == 'Temperature',
          'edge: %s with Temperature and Humidity; linkage %s on Temperature' % (dev.get('name'), ev.get('name')))
    off = (m.get('enabled') == 0 and (docs['edge'] or {}).get('enabled') is False and ev.get('enable') is False and
           not any(t.get('enabled') for n in ('mqttPublish', 'mqttSubscribe') for t in docs[n] or []))
    check(off, 'everything sampled is off: MQTT, Edge, the topics, the linkage')
    print('bench: %s' % ('ok' if not w.failures else 'FAILED (%d)' % len(w.failures)))
    return 1 if w.failures else 0


if __name__ == '__main__':
    sys.exit(main())
