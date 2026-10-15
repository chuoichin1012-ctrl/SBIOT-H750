#!/usr/bin/env python3
"""Anti-clone binding records (spec §11.5): the production side.

The bootloader never makes a record; it only checks the one written here, at programming time,
against the chip's UID and the public key it was built with. The private key never leaves the
production PC.

    provision.py genkey KEY            new 32-byte Ed25519 private key (refuses to overwrite)
    provision.py pubkey KEY PUB        its 32-byte public key, for -DSBIOT_BIND_PUBKEY_FILE=PUB
    provision.py record KEY UID OUT    the 84-byte record of a UID given as 24 hex digits
    provision.py uid CLI_OUTPUT        the UID (hex) in the output of STM32_Programmer_CLI -r32 0x1FF1E800 12

KEY may be "dev": the development key, derived from a public string. A board bound with it runs
only firmware built without SBIOT_BIND_PUBKEY_FILE; never ship it.

Record (little endian): 0 magic "SBBD", 4 UID (12 bytes, as read from 0x1FF1E800), 16 Ed25519
signature (64) over "SBIOT-H750 bind" || UID, 80 CRC-32 of bytes 0..79. tools/flash.sh bind writes
it at QSPI 0x903F0000.
"""
import hashlib
import os
import re
import struct
import sys
import zlib

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

REC_LEN = 84
LABEL = b'SBIOT-H750 bind'
DEV_PUBLIC_HEX = '2a140afad3c3e566a000ad19a5b526b39375b2ab0381391b8bf329b33a89b3cf'


def dev_seed():
    return hashlib.sha256(b'SBIOT-H750 dev bind key').digest()


def public_key(seed):
    pub = Ed25519PrivateKey.from_private_bytes(seed).public_key()
    return pub.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def record(seed, uid):
    if len(uid) != 12:
        raise ValueError('a UID is 12 bytes, got %d' % len(uid))
    sig = Ed25519PrivateKey.from_private_bytes(seed).sign(LABEL + uid)
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
        open(argv[2], 'wb').write(public_key(load_key(argv[1])))
        return 0
    if len(argv) == 4 and argv[0] == 'record':
        open(argv[3], 'wb').write(record(load_key(argv[1]), bytes.fromhex(argv[2])))
        return 0
    if len(argv) == 2 and argv[0] == 'uid':
        print(uid_from_cli(open(argv[1]).read()).hex())
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
