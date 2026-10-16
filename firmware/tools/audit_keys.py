#!/usr/bin/env python3
"""Lists config keys the firmware stores but never reads.

A key in the cfgmodel schema that no runtime module reads is a setting the
web UI offers but the device ignores. Run from the repository root:

    python3 stm32/firmware/tools/audit_keys.py

Keys listed in UNSUPPORTED (with the reason, mirrored in ARCHITECTURE.md)
are accepted and shown as such. Exits 1 while any other key is unread (Plan 11a task 5, the gate of
roadmap row 11; ported from the ESP32 tree's firmware/tools/audit_keys.py).
"""
import glob
import re
import sys

ROOT = "stm32/firmware"
CFG = ROOT + "/components/cfgmodel/cfgmodel.c"
TIMER_KEYS = ("Enabled", "Time", "Action", "Output", "OutputAction")

# Keys accepted but deliberately not acted on, and why.
UNSUPPORTED = {}


def schema_keys():
    src = open(CFG).read()
    keys = {}
    for name, body in re.findall(r"static const field_t (\w+)\[\] = \{(.*?)\};", src, re.S):
        for k in re.findall(r'(?:INT|NUM|BOOL|STR|ENUM)\("([A-Za-z0-9_]+)"', body):
            keys.setdefault(k, set()).add(name)
        for n in re.findall(r"TIMER\((\d)\)", body):
            for t in TIMER_KEYS:
                keys.setdefault(f"timer{n}{t}", set()).add(name)
    return keys


def runtime_sources():
    files = [f for f in glob.glob(ROOT + "/components/**/*.c", recursive=True) + glob.glob(ROOT + "/app/*.c")
             if "/cfgmodel/" not in f and "/www/" not in f]
    return "\n".join(open(f).read() for f in files)


def read_somewhere(key, src):
    if f'"{key}"' in src:
        return True
    # printf-style templates: "timer%dEnabled" covers timer0Enabled..timer5Enabled
    m = re.match(r"([a-zA-Z]+)(\d+)([A-Za-z]*)$", key)
    return bool(m and f'"{m.group(1)}%d{m.group(3)}"' in src)


def main():
    keys, src = schema_keys(), runtime_sources()
    unread = sorted(k for k in keys if not read_somewhere(k, src) and k not in UNSUPPORTED)
    for k, why in sorted(UNSUPPORTED.items()):
        print(f"unsupported  {k:28} {why}")
    for k in unread:
        print(f"UNREAD       {k:28} ({', '.join(sorted(keys[k]))})")
    print(f"{len(keys)} keys, {len(unread)} unread, {len(UNSUPPORTED)} documented as unsupported")
    return 1 if unread else 0


if __name__ == "__main__":
    sys.exit(main())
