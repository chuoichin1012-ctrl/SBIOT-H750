#!/usr/bin/env python3
"""Mutation check of the OTA logic: each mutant must make test_install fail.

    python3 stm32/firmware/test/host/mutants_ota.py      (from the repository root)

Builds test/host/test_install.c once per mutant with the host flags and runs it quickly
(OTA_QUICK=1 skips the power-cut matrices). Every source file is restored afterwards.
Prints one line per mutant and "mutants: N of N caught"; exits 1 when one survives.
"""
import os
import subprocess
import sys
import tempfile

FW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../..'))
EXE = os.path.join(tempfile.gettempdir(), 'test_install_mutant')
BUILD = ('gcc -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g -fsanitize=address,undefined '
         '-fno-sanitize-recover=all -Icomponents/ota/include -Icomponents/board/include -Iboot -Itest/host '
         'test/host/test_install.c test/host/nor_sim.c boot/install.c components/ota/*.c components/board/crc32.c '
         '-o ' + EXE)
# (file, original text, mutated text, what the mutant breaks)
M = [
 ('boot/install.c', '        if (p == 0)\n            memset(buf, 0xFF, sizeof MAGIC);  // the magic is programmed last, on its own\n', '', 'magic programmed with its page'),
 ('boot/install.c', '    if (img_check(dst, dof, in->kick, &back) != IMG_OK || memcmp(back.raw, h->raw, IMG_CORE))\n        return -1;\n', '    (void)back;\n', 'copy not checked'),
 ('boot/install.c', '        if (img_check(in->spi, SPI_FACTORY, in->kick, &hf) != IMG_OK)\n            return halt(in, WHY_FACTORY_BAD);', '        img_check(in->spi, SPI_FACTORY, in->kick, &hf);', 'Reload skips the factory check'),
 ('boot/install.c', "        if (in->spi->erase_sector(in->spi, SPI_STAGING))  // staging's image is never installed again\n            return halt(in, WHY_STAGING);\n", '', 'Reload leaves staging'),
 ('boot/install.c', "        if (in->spi->erase_sector(in->spi, SPI_STAGING))  // staging's image is never installed again\n            return halt(in, WHY_STAGING);\n        if (copy(in, in->spi, SPI_FACTORY, &hf, in->qspi, QSPI_SLOT))\n            return halt(in, WHY_COPY);\n", "        if (copy(in, in->spi, SPI_FACTORY, &hf, in->qspi, QSPI_SLOT))\n            return halt(in, WHY_COPY);\n        if (in->spi->erase_sector(in->spi, SPI_STAGING))  // staging's image is never installed again\n            return halt(in, WHY_STAGING);\n", 'staging erased after the restore'),
 ('boot/install.c', '!(q && !memcmp(hq.raw, hs.raw, IMG_CORE))', '(in->qspi->read(in->qspi, QSPI_SLOT, hq.raw, IMG_CORE), memcmp(hq.raw, hs.raw, IMG_CORE))', 'headers compared, QSPI body ignored'),
 ('boot/install.c', '!(q && !memcmp(hq.raw, hs.raw, IMG_CORE))', '1', 'staging installed every boot'),
 ('boot/install.c', '    if (q && factory_unseeded(in))', '    if (factory_unseeded(in))', 'seeding from a bad QSPI'),
 ('boot/install.c', '    if (q && factory_unseeded(in))', '    if (q && (factory_unseeded(in) || 1))', 'seeding every boot'),
 ('boot/install.c', '== NOR_OK && memcmp(m, MAGIC, sizeof m)', '!= NOR_OK || memcmp(m, MAGIC, sizeof m)', 'unreadable factory taken for unseeded'),
 ('boot/install.c', '    if (q && factory_unseeded(in))  // once, at the first boot after production\n        in->did |= copy(in, in->qspi, QSPI_SLOT, &hq, in->spi, SPI_FACTORY) ? DID_SEED_FAIL : DID_SEED;\n\n    if (img_check(in->spi, SPI_STAGING, in->kick, &hs) == IMG_OK && !(q && !memcmp(hq.raw, hs.raw, IMG_CORE))) {\n        if (copy(in, in->spi, SPI_STAGING, &hs, in->qspi, QSPI_SLOT))\n            return halt(in, WHY_COPY);\n        in->did |= DID_INSTALL;\n        return INSTALL_JUMP;\n    }\n', '    if (img_check(in->spi, SPI_STAGING, in->kick, &hs) == IMG_OK && !(q && !memcmp(hq.raw, hs.raw, IMG_CORE))) {\n        if (copy(in, in->spi, SPI_STAGING, &hs, in->qspi, QSPI_SLOT))\n            return halt(in, WHY_COPY);\n        in->did |= DID_INSTALL;\n        q = img_check(in->qspi, QSPI_SLOT, in->kick, &hq) == IMG_OK;\n        if (q && factory_unseeded(in))\n            in->did |= copy(in, in->qspi, QSPI_SLOT, &hq, in->spi, SPI_FACTORY) ? DID_SEED_FAIL : DID_SEED;\n        return INSTALL_JUMP;\n    }\n    if (q && factory_unseeded(in))\n        in->did |= copy(in, in->qspi, QSPI_SLOT, &hq, in->spi, SPI_FACTORY) ? DID_SEED_FAIL : DID_SEED;\n', 'install before seeding'),
 ('components/ota/ota_image.c', '    if (memcmp(sha, hdr.sha256, 32))\n        return IMG_ESHA;', '', 'SHA not checked'),
 ('components/ota/ota_app.c', '    if (memcmp(sha, st->hdr.sha256, 32))\n        return fail(st, OTA_EIMAGE);', '', 'stage SHA not checked'),
 ('components/ota/ota_app.c', '    if (r != IMG_OK || memcmp(back.raw, st->hdr.raw, IMG_CORE))\n        return fail(st, OTA_EFLASH);', '', 'staging not read back')
]


def main():
    caught = 0
    for path, old, new, what in M:
        path = os.path.join(FW, path)
        src = open(path).read()
        if src.count(old) != 1:
            sys.exit('mutants: the original text of "%s" is not in %s exactly once' % (what, path))
        open(path, 'w').write(src.replace(old, new))
        try:
            b = subprocess.run(BUILD, shell=True, cwd=FW, capture_output=True, text=True)
            if b.returncode:
                res = 'BUILD ERROR (fix the mutant): ' + (b.stderr.splitlines() or [''])[0]
            else:
                env = dict(os.environ, OTA_QUICK='1', ASAN_OPTIONS='detect_leaks=0')
                r = subprocess.run([EXE], capture_output=True, text=True, timeout=600, env=env)
                res = 'caught' if r.returncode else 'SURVIVED'
                caught += r.returncode != 0
        finally:
            open(path, 'w').write(src)
        print('%-36s %s' % (what, res))
    print('mutants: %d of %d caught' % (caught, len(M)))
    return 0 if caught == len(M) else 1


if __name__ == '__main__':
    sys.exit(main())
