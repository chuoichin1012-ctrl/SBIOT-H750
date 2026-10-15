#!/usr/bin/env python3
"""Checks mkimage.py and writes a sample image for the C side to verify.

    python3 stm32/firmware/tools/test_mkimage.py OUT_IMG

OUT_IMG gets a 1.2.3 image of a 5 000-byte body; test/host/test_ota.c checks it with img_check().
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkimage  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(('ok   ' if ok else 'FAIL ') + what)
    failures += not ok


def app_bin(body_len, msp=0x20020000, reset=0x900005E9):
    body = struct.pack('<II', msp, reset) + bytes((i * 7) & 0xFF for i in range(body_len - 8))
    return b'\xff' * mkimage.HDR + body


def refused(data):
    try:
        mkimage.make(data, (1, 2, 3))
        return False
    except ValueError:
        return True


img = mkimage.make(app_bin(5000), (1, 2, 3))
check(len(img) == mkimage.HDR + 5000 and img[:4] == b'SBIO', 'header in front of the body')
check(refused(b'\x00' + app_bin(5000)[1:]), 'refuses a binary without the 0xFF placeholder')
check(refused(app_bin(5000, msp=0x30000000)), 'refuses an MSP outside DTCM/AXI')
check(refused(app_bin(5000, reset=0x90000400)), 'refuses a reset vector without the Thumb bit')
check(refused(app_bin(5000, reset=0x90000400 + 5001)), 'refuses a reset vector past the body')
check(refused(app_bin(mkimage.SLOT - mkimage.HDR + 4)), 'refuses a body larger than the slot')
open(sys.argv[1], 'wb').write(img)
print('test_mkimage: %s' % ('ok' if not failures else 'FAILED'))
sys.exit(1 if failures else 0)
