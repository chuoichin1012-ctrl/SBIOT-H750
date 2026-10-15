#!/usr/bin/env python3
"""Checks tools/provision.py and writes the sample the C test of the binding reads:

    python3 stm32/firmware/tools/test_provision.py OUT.bin

OUT.bin: dev public key (32) | UID A (12) | record of A (84) | UID B (12) | record of A signed with
another key (84). test/host/test_bind.c checks these with Monocypher, so Python's signer and the
bootloader's check are proven against each other.
"""
import os
import stat
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import provision  # noqa: E402
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(('ok   ' if ok else 'FAIL ') + what)
    failures += not ok


UID_A = bytes(range(0x10, 0x1C))
UID_B = UID_A[:11] + bytes([UID_A[11] ^ 1])


def test_record():
    seed = provision.dev_seed()
    pub = provision.public_key(seed)
    check(pub.hex() == provision.DEV_PUBLIC_HEX, 'the dev public key is the one the build embeds')
    rec = provision.record(seed, UID_A)
    check(len(rec) == provision.REC_LEN == 84, 'record is 84 bytes')
    check(rec[:4] == b'SBBD' and rec[4:16] == UID_A, 'magic, then the UID')
    check(struct.unpack('<I', rec[80:84])[0] == zlib.crc32(rec[:80]), 'CRC-32 of bytes 0..79 at 80')
    try:
        Ed25519PublicKey.from_public_bytes(pub).verify(rec[16:80], b'SBIOT-H750 bind' + UID_A)
        ok = True
    except Exception:
        ok = False
    check(ok, 'the signature verifies over "SBIOT-H750 bind" || UID')
    check(provision.record(seed, UID_A) == rec, 'Ed25519 is deterministic: same record twice')
    try:
        provision.record(seed, UID_A[:11])
        check(False, 'a UID that is not 12 bytes is refused')
    except ValueError:
        check(True, 'a UID that is not 12 bytes is refused')


def test_uid_from_cli():
    out = ('      -------------------------------------------------------------------\n'
           'Reading 32-bit memory content\n  Size          : 12 Bytes\n  Address:      : 0x1FF1E800\n\n'
           '0x1FF1E800 : 0034001F 30385104 32383339\n')
    uid = provision.uid_from_cli(out)
    check(uid == struct.pack('<3I', 0x0034001F, 0x30385104, 0x32383339), 'UID parsed from CubeProgrammer -r32 output, little endian')
    try:
        provision.uid_from_cli('Error: no device found\n')
        check(False, 'output without the UID line is refused')
    except ValueError:
        check(True, 'output without the UID line is refused')


def test_genkey():
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 'bind.key')
        provision.main(['genkey', p])
        check(os.path.getsize(p) == 32, 'genkey writes a 32-byte private key')
        check(stat.S_IMODE(os.stat(p).st_mode) == 0o600, 'readable by its owner only')
        first = open(p, 'rb').read()
        check(provision.main(['genkey', p]) != 0 and open(p, 'rb').read() == first, 'genkey never overwrites a key')
        pubp = os.path.join(d, 'bind.pub')
        provision.main(['pubkey', p, pubp])
        check(open(pubp, 'rb').read() == provision.public_key(first), 'pubkey writes the 32-byte public key')
        recp = os.path.join(d, 'rec.bin')
        provision.main(['record', p, UID_A.hex(), recp])
        check(open(recp, 'rb').read() == provision.record(first, UID_A), 'record writes the record of a hex UID')


def write_sample(path):
    seed = provision.dev_seed()
    other = bytes(range(32))
    data = (provision.public_key(seed) + UID_A + provision.record(seed, UID_A) + UID_B +
            provision.record(other, UID_A))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    open(path, 'wb').write(data)


test_record()
test_uid_from_cli()
test_genkey()
if len(sys.argv) > 1 and not failures:
    write_sample(sys.argv[1])
print('test_provision: %s' % ('ok' if not failures else 'FAILED'))
sys.exit(1 if failures else 0)
