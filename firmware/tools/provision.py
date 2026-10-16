#!/usr/bin/env python3
"""Anti-clone binding records (spec §11.5): the production side.

The bootloader never makes a record; it only checks the one written here, at programming time,
against the chip's UID and the public key it was built with. The private key never leaves the
production PC.

    provision.py genkey KEY            new 32-byte Ed25519 private key (refuses to overwrite)
    provision.py pubkey KEY PUB        its 32-byte public key, for -DSBIOT_BIND_PUBKEY_FILE=PUB
    provision.py record KEY UID SN OUT the 104-byte record of a UID given as 24 hex digits, with its SN
    provision.py uid CLI_OUTPUT        the UID (hex) in the output of STM32_Programmer_CLI -r32 0x1FF1E800 12

KEY may be "dev": the development key, derived from a public string. A board bound with it runs
only firmware built without SBIOT_BIND_PUBKEY_FILE; never ship it.

Record version 2 (Plan 11a, little endian): 0 magic "SBB2", 4 UID (12 bytes, as read from 0x1FF1E800),
16 MAC (6: 02 then SHA-256(UID)[0..4], computed here), 22 SN (12 ASCII: SB, YYMM, 6 digits), 34 reserved (2,
zero), 36 Ed25519 signature (64) over "SBIOT-H750 bind2" || bytes 4..33, 100 CRC-32 of bytes 0..99.
tools/flash.sh bind writes it at QSPI 0x903F0000. Version 1 ("SBBD", 84 bytes, no MAC or SN) halts the
firmware of this version (the user, 2026-10-17).
"""
import hashlib
import os
import re
import struct
import sys
import zlib

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

REC_LEN = 104
LABEL = b'SBIOT-H750 bind2'
SN_RE = re.compile(r'^SB\d{10}$')
DEV_PUBLIC_HEX = '2a140afad3c3e566a000ad19a5b526b39375b2ab0381391b8bf329b33a89b3cf'


def dev_seed():
    return hashlib.sha256(b'SBIOT-H750 dev bind key').digest()


def public_key(seed):
    pub = Ed25519PrivateKey.from_private_bytes(seed).public_key()
    return pub.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def mac_of(uid):
    """The MAC the record carries: locally administered, 02 then SHA-256(UID)[0..4] (the firmware's netcfg_mac)."""
    return b'\x02' + hashlib.sha256(uid).digest()[:5]


def record(seed, uid, sn):
    if len(uid) != 12:
        raise ValueError('a UID is 12 bytes, got %d' % len(uid))
    if not isinstance(sn, str) or not SN_RE.match(sn):
        raise ValueError('an SN is SB then 10 digits (SB + YYMM + 6), got %r' % (sn,))
    signed = uid + mac_of(uid) + sn.encode('ascii')
    sig = Ed25519PrivateKey.from_private_bytes(seed).sign(LABEL + signed)
    body = b'SBB2' + signed + b'\0\0' + sig
    return body + struct.pack('<I', zlib.crc32(body))


def record_v1(seed, uid):
    """The record of the firmware before Plan 11a (84 bytes, no MAC or SN): only for the tests."""
    sig = Ed25519PrivateKey.from_private_bytes(seed).sign(b'SBIOT-H750 bind' + uid)
    body = b'SBBD' + uid + sig
    return body + struct.pack('<I', zlib.crc32(body))


def uid_from_cli(text):
    m = re.search(r'0x1FF1E800\s*:\s*([0-9A-F]{8})\s+([0-9A-F]{8})\s+([0-9A-F]{8})', text, re.I)
    if not m:
        raise ValueError('no UID in the CubeProgrammer output')
    return struct.pack('<3I', *(int(w, 16) for w in m.groups()))


def load_key(path):
    if path == 'dev':
        return dev_seed()
    seed = open(path, 'rb').read()
    if len(seed) != 32:
        raise ValueError('%s: a private key is 32 bytes, got %d' % (path, len(seed)))
    return seed


def write_out(path, data, key):
    """Writes data to path, computed before; never over the key file (also through a link)."""
    if key != 'dev' and os.path.exists(path) and os.path.samefile(path, key):
        print('provision: %s is the private key, not overwritten' % path, file=sys.stderr)
        return 1
    with open(path, 'wb') as f:
        f.write(data)
    return 0


def main(argv):
    if len(argv) >= 2 and argv[0] == 'genkey':
        try:
            fd = os.open(argv[1], os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except FileExistsError:
            print('provision: %s exists, not overwritten' % argv[1], file=sys.stderr)
            return 1
        with os.fdopen(fd, 'wb') as f:
            f.write(os.urandom(32))
        return 0
    if len(argv) == 3 and argv[0] == 'pubkey':
        return write_out(argv[2], public_key(load_key(argv[1])), argv[1])
    if len(argv) == 5 and argv[0] == 'record':
        return write_out(argv[4], record(load_key(argv[1]), bytes.fromhex(argv[2]), argv[3]), argv[1])
    if len(argv) == 2 and argv[0] == 'uid':
        print(uid_from_cli(open(argv[1]).read()).hex())
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
