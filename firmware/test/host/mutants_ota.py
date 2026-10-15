#!/usr/bin/env python3
"""Mutation check of the OTA and anti-clone logic: each mutant must make its test fail.

    python3 stm32/firmware/test/host/mutants_ota.py      (from the repository root)

Builds the mutant's test (test/host/test_install.c, or test_bind.c for the binding) with the host
flags and runs it quickly (OTA_QUICK=1 skips the install power-cut matrices). Every source file is restored afterwards.
Prints one line per mutant and "mutants: N of N caught"; exits 1 when one survives.
"""
import os
import subprocess
import sys
import tempfile

FW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../..'))
EXE = os.path.join(tempfile.gettempdir(), 'test_ota_mutant')
CC = ('gcc -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined '
      '-fno-sanitize-recover=all -Icomponents/ota/include -Icomponents/board/include -Iboot -Itest/host ')
BUILD = {
    'install': CC + 'test/host/test_install.c test/host/nor_sim.c boot/install.c components/ota/sha256.c '
               'components/ota/ota_image.c components/ota/ota_app.c components/board/crc32.c -o ' + EXE,
    'bind': CC + '-Ithird_party/monocypher test/host/test_bind.c test/host/nor_sim.c components/ota/bind.c '
            'third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c '
            'components/board/crc32.c -o ' + EXE,
}
# test_bind reads the records tools/test_provision.py signs (main() writes them first).
SAMPLE = os.path.join(FW, 'test/host/build/bind_sample.bin')
ARGS = {'install': [], 'bind': [SAMPLE]}
# (file, original text, mutated text, what the mutant breaks[, test: 'install' by default])
M = [
 ('boot/install.c', '        if (p == 0)\n            memset(buf, 0xFF, sizeof IMG_MAGIC);  // the magic is programmed last, on its own\n', '', 'magic programmed with its page'),
 ('boot/install.c', '    if (img_check(dst, dof, in->kick, &back) != IMG_OK || memcmp(back.raw, h->raw, IMG_CORE))\n        return -1;\n', '    (void)back;\n', 'copy not checked'),
 ('boot/install.c', '        if (img_check(in->spi, SPI_FACTORY, in->kick, &hf) != IMG_OK)\n            return halt(in, WHY_FACTORY_BAD);', '        img_check(in->spi, SPI_FACTORY, in->kick, &hf);', 'Reload skips the factory check'),
 ('boot/install.c', "        if (in->spi->erase_sector(in->spi, SPI_STAGING))  // staging's image is never installed again\n            return halt(in, WHY_STAGING);\n", '', 'Reload leaves staging'),
 ('boot/install.c', "        if (in->spi->erase_sector(in->spi, SPI_STAGING))  // staging's image is never installed again\n            return halt(in, WHY_STAGING);\n        if (copy(in, in->spi, SPI_FACTORY, &hf, in->qspi, QSPI_SLOT))\n            return halt(in, WHY_COPY);\n", "        if (copy(in, in->spi, SPI_FACTORY, &hf, in->qspi, QSPI_SLOT))\n            return halt(in, WHY_COPY);\n        if (in->spi->erase_sector(in->spi, SPI_STAGING))  // staging's image is never installed again\n            return halt(in, WHY_STAGING);\n", 'staging erased after the restore'),
 ('boot/install.c', '!(q && !memcmp(hq.raw, hs.raw, IMG_CORE))', '(in->qspi->read(in->qspi, QSPI_SLOT, hq.raw, IMG_CORE), memcmp(hq.raw, hs.raw, IMG_CORE))', 'headers compared, QSPI body ignored'),
 ('boot/install.c', '!(q && !memcmp(hq.raw, hs.raw, IMG_CORE))', '1', 'staging installed every boot'),
 ('boot/install.c', '    if (q && factory_unseeded(in))', '    if (factory_unseeded(in))', 'seeding from a bad QSPI'),
 ('boot/install.c', '    if (q && factory_unseeded(in))', '    if (q && (factory_unseeded(in) || 1))', 'seeding every boot'),
 ('boot/install.c', '== NOR_OK && memcmp(m, IMG_MAGIC, sizeof m)', '!= NOR_OK || memcmp(m, IMG_MAGIC, sizeof m)', 'unreadable factory taken for unseeded'),
 ('boot/install.c', '    if (q && factory_unseeded(in))  // once, at the first boot after production\n        in->did |= copy(in, in->qspi, QSPI_SLOT, &hq, in->spi, SPI_FACTORY) ? DID_SEED_FAIL : DID_SEED;\n\n    if (img_check(in->spi, SPI_STAGING, in->kick, &hs) == IMG_OK && !(q && !memcmp(hq.raw, hs.raw, IMG_CORE))) {\n        if (copy(in, in->spi, SPI_STAGING, &hs, in->qspi, QSPI_SLOT))\n            return halt(in, WHY_COPY);\n        in->did |= DID_INSTALL;\n        return INSTALL_JUMP;\n    }\n', '    if (img_check(in->spi, SPI_STAGING, in->kick, &hs) == IMG_OK && !(q && !memcmp(hq.raw, hs.raw, IMG_CORE))) {\n        if (copy(in, in->spi, SPI_STAGING, &hs, in->qspi, QSPI_SLOT))\n            return halt(in, WHY_COPY);\n        in->did |= DID_INSTALL;\n        q = img_check(in->qspi, QSPI_SLOT, in->kick, &hq) == IMG_OK;\n        if (q && factory_unseeded(in))\n            in->did |= copy(in, in->qspi, QSPI_SLOT, &hq, in->spi, SPI_FACTORY) ? DID_SEED_FAIL : DID_SEED;\n        return INSTALL_JUMP;\n    }\n    if (q && factory_unseeded(in))\n        in->did |= copy(in, in->qspi, QSPI_SLOT, &hq, in->spi, SPI_FACTORY) ? DID_SEED_FAIL : DID_SEED;\n', 'install before seeding'),
 ('components/ota/ota_image.c', '    if (memcmp(sha, hdr.sha256, 32))\n        return IMG_ESHA;', '', 'SHA not checked'),
 ('components/ota/ota_app.c', '    if (memcmp(sha, st->hdr.sha256, 32))\n        return fail(st, OTA_EIMAGE);', '', 'stage SHA not checked'),
 ('components/ota/ota_app.c', '    if (r != IMG_OK || memcmp(back.raw, st->hdr.raw, IMG_CORE))\n        return fail(st, OTA_EFLASH);', '', 'staging not read back'),
 ('boot/install.c', '    if (same(in, src, so, dst, dof, len))\n        return -1;\n', '    (void)same;\n', 'copy not read back before the magic'),
 ('components/ota/ota_app.c', '    if (at == 0)\n        memset(st->page, 0xFF, sizeof IMG_MAGIC);  // the magic only once the whole image is in (finish)\n', '', 'upload writes the magic early'),
 ('components/ota/ota_app.c', '    if (spi->erase_sector(spi, SPI_STAGING))\n        return fail(st, OTA_EFLASH);\n    st->erased = NOR_SECTOR;\n', '', 'upload leaves the old staged image'),
 ('components/ota/bind.c', '    return crypto_ed25519_check(rec + 16, pub, msg, sizeof msg) == 0;', '    return true;', 'signature not checked', 'bind'),
 ('components/ota/bind.c', '    memcpy(msg + sizeof LABEL, uid, BIND_UID_LEN);', '    memcpy(msg + sizeof LABEL, rec + 4, BIND_UID_LEN);', 'signature over the stored UID', 'bind'),
 ('components/ota/bind.c', '    if (!crc_ok(rec))\n        return BIND_ECORRUPT;', '', 'corrupt record taken for a clone', 'bind'),
 ('components/ota/bind.c', '        qspi->erase_sector(qspi, QSPI_SLOT + s);', '        (void)s;', 'clone not erased', 'bind'),
 ('components/ota/bind.c', '    return BIND_CLONE;\n}', '    qspi->erase_sector(qspi, QSPI_BIND);\n    return BIND_CLONE;\n}', 'clone erases the record too', 'bind'),
 ('components/ota/bind.c', '        return BIND_ENONE;  // never provisioned', '        return BIND_OK;  // never provisioned', 'unprovisioned board runs', 'bind'),
 ('components/ota/bind.c', '    if (qspi->read(qspi, QSPI_BIND, rec, BIND_LEN))\n        return BIND_EREAD;', '    if (qspi->read(qspi, QSPI_BIND, rec, BIND_LEN))\n        memset(rec, 0, BIND_LEN);', 'read error taken for a record', 'bind')
]


def main():
    subprocess.run(['make', '-s', '-C', os.path.join(FW, 'test/host'), 'build/bind_sample.bin'], check=True)
    caught = 0
    for path, old, new, what, *test in M:
        path = os.path.join(FW, path)
        src = open(path).read()
        if src.count(old) != 1:
            sys.exit('mutants: the original text of "%s" is not in %s exactly once' % (what, path))
        open(path, 'w').write(src.replace(old, new))
        try:
            b = subprocess.run(BUILD[test[0] if test else 'install'], shell=True, cwd=FW, capture_output=True, text=True)
            if b.returncode:
                res = 'BUILD ERROR (fix the mutant): ' + (b.stderr.splitlines() or [''])[0]
            else:
                env = dict(os.environ, OTA_QUICK='1', ASAN_OPTIONS='detect_leaks=0')
                r = subprocess.run([EXE] + ARGS[test[0] if test else 'install'], capture_output=True, text=True, timeout=600, env=env)
                res = 'caught' if r.returncode else 'SURVIVED'
                caught += r.returncode != 0
        finally:
            open(path, 'w').write(src)
        print('%-36s %s' % (what, res))
    print('mutants: %d of %d caught' % (caught, len(M)))
    return 0 if caught == len(M) else 1


if __name__ == '__main__':
    sys.exit(main())
