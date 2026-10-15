#!/usr/bin/env python3
"""Fills the 1 KB image header (spec §11.1) of the app binary.

    python3 stm32/firmware/tools/mkimage.py --version 0.4.0 build/h750/app.bin build/h750/app.img

app.bin comes from objcopy of app.elf and starts at 0x90000000 with the header placeholder (1 KB
of 0xFF, app/image_header.c); the body (vector table first) follows at 0x90000400. The header
layout is the one components/ota/ota_image.h documents and img_parse() checks.
"""
import argparse
import hashlib
import struct
import sys
import zlib

HDR = 0x400
SLOT = 0x170000
XIP = 0x90000000
HW = b'SBIOT-H750'


def header(body, version):
    core = b'SBIO' + HW.ljust(16, b'\0') + struct.pack('<4H', *version, 0) + struct.pack('<I', len(body))
    core += hashlib.sha256(body).digest()
    core += struct.pack('<I', zlib.crc32(core))
    return core + b'\xff' * (HDR - len(core))


def vectors_ok(body):
    msp, reset = struct.unpack('<II', body[:8])
    in_ram = lambda sp, base, size: base < sp <= base + size
    msp_ok = msp % 8 == 0 and (in_ram(msp, 0x20000000, 128 * 1024) or in_ram(msp, 0x24000000, 512 * 1024))
    pc = reset & ~1
    return msp_ok and reset & 1 == 1 and XIP + HDR <= pc < XIP + HDR + len(body)


def make(data, version):
    """Returns the image, or raises ValueError with the reason."""
    if data[:HDR] != b'\xff' * HDR:
        raise ValueError('the first 1 KB is not the erased header placeholder (app/image_header.c)')
    body = data[HDR:]
    if not 8 <= len(body) <= SLOT - HDR:
        raise ValueError('body of %d bytes does not fit the %d-byte slot' % (len(body), SLOT))
    if not vectors_ok(body):
        raise ValueError('initial MSP or reset vector unusable')
    return header(body, version) + body


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--version', required=True, help='major.minor.patch')
    ap.add_argument('bin')
    ap.add_argument('img')
    a = ap.parse_args()
    version = tuple(int(x) for x in a.version.split('.'))
    if len(version) != 3 or not all(0 <= v < 65536 for v in version):
        sys.exit('mkimage: version must be major.minor.patch')
    try:
        img = make(open(a.bin, 'rb').read(), version)
    except ValueError as e:
        sys.exit('mkimage: %s' % e)
    open(a.img, 'wb').write(img)
    print('mkimage: %s %s, body %d bytes, sha256 %s' % (a.img, a.version, len(img) - HDR,
                                                        hashlib.sha256(img[HDR:]).hexdigest()[:16]))


if __name__ == '__main__':
    main()
