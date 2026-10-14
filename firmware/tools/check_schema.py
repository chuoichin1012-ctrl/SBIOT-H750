#!/usr/bin/env python3
"""Checks that the web UI's field ids and the firmware's config schema agree.

The firmware drops keys it does not know (cfgmodel), so a field the UI sends
under a name the schema lacks is silently lost on save. Run from the
repository root after changing a FieldDef or cfgmodel.c:

    python3 stm32/firmware/tools/check_schema.py

Exits 1 on any field the UI sends that the firmware would drop.
"""
import re
import sys

CFG = "stm32/firmware/components/cfgmodel/cfgmodel.c"
WEB = "stm32/web/src"
TIMER_KEYS = ("Enabled", "Time", "Action", "Output", "OutputAction")


def firmware_schema():
    src = open(CFG).read()
    out = {}
    for name, body in re.findall(r"static const field_t (\w+)\[\] = \{(.*?)\};", src, re.S):
        keys = set(re.findall(r'(?:INT|NUM|BOOL|STR|ENUM)\("([A-Za-z0-9_]+)"', body))
        for n in re.findall(r"TIMER\((\d)\)", body):
            keys |= {f"timer{n}{k}" for k in TIMER_KEYS}
        out[name] = keys
    return out


def ids(text):
    found = set(re.findall(r"id: '([A-Za-z0-9_]+)'", text))
    if "`timer${t}" in text:  # Io.tsx: six timers, 0-based
        found |= {f"timer{t}{k}" for t in range(6) for k in TIMER_KEYS}
    if "`do${n - 1}Mode`" in text:  # Io.tsx: two DOs
        found |= {f"do{i}{k}" for i in range(2) for k in ("Mode", "FlipDelay")}
    return found


def section(path, start, end=None):
    text = open(path).read()
    a = text.index(start)
    return text[a : text.index(end, a)] if end else text[a:]


def main():
    fw = firmware_schema()
    checks = {
        "network_fields (network)": (ids(open(f"{WEB}/pages/Network.tsx").read()), fw["network_fields"]),
        "port_fields (port1)": (ids(open(f"{WEB}/pages/Port.tsx").read()), fw["port_fields"]),
        "mqtt_fields (mqtt)": (
            ids(section(f"{WEB}/pages/Mqtt.tsx", "const brokerDefs", "/** One publish"))
            | ids(section(f"{WEB}/pages/Mqtt.tsx", "function customDefs", "function TopicEditor")),
            fw["mqtt_fields"],
        ),
        "topic_fields (mqttPublish/Subscribe)": (
            ids(section(f"{WEB}/pages/Mqtt.tsx", "function topicDefs", "/**\n * Custom mode")) | {"enabled"},
            fw["topic_fields"],
        ),
        "system_fields (system)": (ids(open(f"{WEB}/pages/System.tsx").read()), fw["system_fields"]),
        "io_fields (io)": (ids(open(f"{WEB}/pages/Io.tsx").read()) - {"ao0Mode"}, fw["io_fields"]),
        "edge_report_fields (edgeReport)": (ids(open(f"{WEB}/pages/edge/Report.tsx").read()), fw["edge_report_fields"]),
    }
    bad = False
    for name, (web, schema) in checks.items():
        dropped, unused = sorted(web - schema), sorted(schema - web)
        print(f"{name:38} dropped by firmware: {dropped or '-'}   schema-only: {unused or '-'}")
        bad |= bool(dropped)
    print("MISMATCH: fix the names above" if bad else "consistent")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
