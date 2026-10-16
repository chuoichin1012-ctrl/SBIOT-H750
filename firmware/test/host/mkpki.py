#!/usr/bin/env python3
"""Test certificates for Plan 08b (host tests and the bench), with exact validity dates.

    python3 mkpki.py OUTDIR [--ip A ...] [--dns NAME ...] [--wrong-ip W] [--not-before YYYY-MM-DD | --now] [--days N]

Writes PEM files to OUTDIR:
  ca.pem               the CA the gateway is given
  server.pem/.key      RSA-2048, SANs from --ip/--dns, signed by ca
  server_ec.pem/.key   the same with an EC P-256 key (ECDHE-ECDSA)
  wrongsan.pem/.key    signed by ca, its only SAN is --wrong-ip
  ca2.pem, other.pem/.key   another CA, and a server certificate with the same SANs that it signed
  client.pem/.key      CN=gateway, signed by ca (mutual TLS)
  ec.key               a lone EC P-256 key (PKCS#8); enc.key: an encrypted key
"""
import argparse
import datetime
import ipaddress
import os

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.x509.oid import NameOID


def name(cn):
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])


def rsa_key():
    return rsa.generate_private_key(public_exponent=65537, key_size=2048)


def cert(subject_cn, key, issuer_cn, issuer_key, nb, na, sans=None, ca=False):
    b = (x509.CertificateBuilder().subject_name(name(subject_cn)).issuer_name(name(issuer_cn))
         .public_key(key.public_key()).serial_number(x509.random_serial_number())
         .not_valid_before(nb).not_valid_after(na)
         .add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True))
    if sans:
        b = b.add_extension(x509.SubjectAlternativeName(sans), critical=False)
    return b.sign(issuer_key, hashes.SHA256())


def write(out, fname, obj, password=None):
    if isinstance(obj, x509.Certificate):
        data = obj.public_bytes(serialization.Encoding.PEM)
    else:
        enc = serialization.BestAvailableEncryption(password) if password else serialization.NoEncryption()
        data = obj.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, enc)
    with open(os.path.join(out, fname), 'wb') as f:
        f.write(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--ip', action='append', default=[])
    ap.add_argument('--dns', action='append', default=[])
    ap.add_argument('--wrong-ip', default='192.0.2.99')
    ap.add_argument('--not-before', default='2026-01-01')
    ap.add_argument('--now', action='store_true', help='valid from an hour ago (the bench)')
    ap.add_argument('--days', type=int, default=2)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    if a.now:
        nb = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(hours=1)
    else:
        nb = datetime.datetime.fromisoformat(a.not_before).replace(tzinfo=datetime.timezone.utc)
    na = nb + datetime.timedelta(days=a.days)
    ca_nb, ca_na = nb - datetime.timedelta(days=1), nb + datetime.timedelta(days=3650)
    sans = [x509.IPAddress(ipaddress.ip_address(i)) for i in a.ip] + [x509.DNSName(d) for d in a.dns]

    ca_key, ca2_key = rsa_key(), rsa_key()
    write(a.out, 'ca.pem', cert('SBIOT Test CA', ca_key, 'SBIOT Test CA', ca_key, ca_nb, ca_na, ca=True))
    write(a.out, 'ca2.pem', cert('Other CA', ca2_key, 'Other CA', ca2_key, ca_nb, ca_na, ca=True))
    k = rsa_key()
    write(a.out, 'server.key', k)
    write(a.out, 'server.pem', cert('sbiot-test-server', k, 'SBIOT Test CA', ca_key, nb, na, sans))
    k = ec.generate_private_key(ec.SECP256R1())
    write(a.out, 'server_ec.key', k)
    write(a.out, 'server_ec.pem', cert('sbiot-test-server', k, 'SBIOT Test CA', ca_key, nb, na, sans))
    k = rsa_key()
    write(a.out, 'wrongsan.key', k)
    write(a.out, 'wrongsan.pem', cert('sbiot-wrong-san', k, 'SBIOT Test CA', ca_key, nb, na,
                                      [x509.IPAddress(ipaddress.ip_address(a.wrong_ip))]))
    k = rsa_key()
    write(a.out, 'other.key', k)
    write(a.out, 'other.pem', cert('sbiot-other', k, 'Other CA', ca2_key, nb, na, sans))
    k = rsa_key()
    write(a.out, 'client.key', k)
    write(a.out, 'client.pem', cert('gateway', k, 'SBIOT Test CA', ca_key, nb, na))
    write(a.out, 'ec.key', ec.generate_private_key(ec.SECP256R1()))
    write(a.out, 'enc.key', rsa_key(), password=b'secret')


if __name__ == '__main__':
    main()
