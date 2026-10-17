#!/usr/bin/env python3
"""Checks tools/provision.py and writes the sample the C test of the binding reads:

    python3 stm32/firmware/tools/test_provision.py OUT.bin

OUT.bin: dev public key (32) | UID A (12) | record of A (104) | UID B (12) | record of A signed with
another key (104) | an old (version 1) record of A (84, then 0xFF to 104). test/host/test_bind.c checks
these with Monocypher, so Python's signer and the bootloader's check are proven against each other.
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


SN_A = 'SB2610000001'
# netcfg_mac's vector (test/host/test_netcfg.c): 02 then SHA-256(UID)[0..4]; the tool computes the same.
NETCFG_UID = bytes([0x30, 0x00, 0x47, 0x00, 0x13, 0x51, 0x33, 0x34, 0x37, 0x38, 0x36, 0x31])


def test_record():
    seed = provision.dev_seed()
    pub = provision.public_key(seed)
    check(pub.hex() == provision.DEV_PUBLIC_HEX, 'the dev public key is the one the build embeds')
    rec = provision.record(seed, UID_A, SN_A)
    mac = provision.mac_of(UID_A)
    check(len(rec) == provision.REC_LEN == 104, 'record is 104 bytes (version 2, Plan 11a)')
    check(rec[:4] == b'SBB2' and rec[4:16] == UID_A and rec[16:22] == mac and rec[22:34] == SN_A.encode()
          and rec[34:36] == b'\0\0', 'magic, UID, MAC, SN, reserved')
    check(struct.unpack('<I', rec[100:104])[0] == zlib.crc32(rec[:100]), 'CRC-32 of bytes 0..99 at 100')
    try:
        Ed25519PublicKey.from_public_bytes(pub).verify(rec[36:100], b'SBIOT-H750 bind2' + rec[4:34])
        ok = True
    except Exception:
        ok = False
    check(ok, 'the signature verifies over "SBIOT-H750 bind2" || UID || MAC || SN')
    check(provision.record(seed, UID_A, SN_A) == rec, 'Ed25519 is deterministic: same record twice')
    import hashlib
    check(provision.mac_of(NETCFG_UID) == b'\x02' + hashlib.sha256(NETCFG_UID).digest()[:5] and mac[0] == 2,
          'the MAC is netcfg_mac\'s: 02 then SHA-256(UID)[0..4]')
    for bad_uid, bad_sn, what in ((UID_A[:11], SN_A, 'a UID that is not 12 bytes'),
                                  (UID_A, 'SB26100000', 'an SN of 10 characters'),
                                  (UID_A, 'XB2610000001', 'an SN without SB'),
                                  (UID_A, 'SB26100000a1', 'an SN with a letter')):
        try:
            provision.record(seed, bad_uid, bad_sn)
            check(False, '%s is refused' % what)
        except ValueError:
            check(True, '%s is refused' % what)


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
        link = os.path.join(d, 'link.key')
        os.symlink(p, link)
        hard = os.path.join(d, 'hard.key')
        os.link(p, hard)
        for out, how in ((p, 'the key itself'), (link, 'a symlink to it'), (hard, 'a hard link to it')):
            rp = provision.main(['pubkey', p, out])
            rr = provision.main(['record', p, UID_A.hex(), SN_A, out])
            check(rp != 0 and rr != 0 and open(p, 'rb').read() == first,
                  'pubkey/record refuse to write over %s, and the key is unchanged' % how)
        bad = os.path.join(d, 'short.key')
        open(bad, 'wb').write(b'x' * 31)
        outp = os.path.join(d, 'out.bin')
        open(outp, 'wb').write(b'keep')
        try:
            provision.main(['pubkey', bad, outp])
        except ValueError:
            pass
        check(open(outp, 'rb').read() == b'keep', 'a bad key is refused before the output is touched')
        recp = os.path.join(d, 'rec.bin')
        provision.main(['record', p, UID_A.hex(), SN_A, recp])
        check(open(recp, 'rb').read() == provision.record(first, UID_A, SN_A), 'record writes the record of a hex UID and an SN')


def test_next_sn():
    import datetime
    import multiprocessing
    with tempfile.TemporaryDirectory() as d:
        ctr = os.path.join(d, 'sn_counter')
        yymm = datetime.date.today().strftime('%y%m')
        a, b = provision.next_sn(ctr), provision.next_sn(ctr)
        check(a == 'SB' + yymm + '000001' and b == 'SB' + yymm + '000002', 'consecutive SNs from a new counter (%s %s)' % (a, b))
        with multiprocessing.Pool(8) as pool:
            got = pool.map(provision.next_sn, [ctr] * 8)
        check(len(set(got)) == 8 and all(provision.SN_RE.match(x) for x in got), '8 processes at once: 8 different SNs')
        before = open(ctr).read()
        real = os.replace
        def fail(*a):
            raise OSError('disk full')
        os.replace = fail
        try:
            provision.next_sn(ctr)
            check(False, 'a failed write gives no SN')
        except OSError:
            check(open(ctr).read() == before, 'a failed write gives no SN and leaves the counter as it was')
        finally:
            os.replace = real
        open(ctr, 'w').write('999999')
        check(provision.next_sn(ctr).endswith('999999'), 'the last number, 999999, is handed out')
        try:
            provision.next_sn(ctr)
            check(False, 'past 999999: refused')
        except ValueError:
            check(True, 'past 999999: refused')


def write_sample(path):
    seed = provision.dev_seed()
    other = bytes(range(32))
    data = (provision.public_key(seed) + UID_A + provision.record(seed, UID_A, SN_A) + UID_B +
            provision.record(other, UID_A, SN_A) + provision.record_v1(seed, UID_A).ljust(provision.REC_LEN, b'\xff'))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    open(path, 'wb').write(data)


test_record()
test_next_sn()
test_uid_from_cli()
test_genkey()
if len(sys.argv) > 1 and not failures:
    write_sample(sys.argv[1])
print('test_provision: %s' % ('ok' if not failures else 'FAILED'))
sys.exit(1 if failures else 0)
