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
FDB = ('third_party/flashdb/src/fdb.c third_party/flashdb/src/fdb_kvdb.c third_party/flashdb/src/fdb_tsdb.c '
       'third_party/flashdb/src/fdb_utils.c third_party/flashdb/port/fal/src/fal.c '
       'third_party/flashdb/port/fal/src/fal_flash.c third_party/flashdb/port/fal/src/fal_partition.c')
STORE = (CC.replace('-Werror ', '') + '-Wno-implicit-function-declaration -Icomponents/store/include -Icomponents/store/port '
         '-Ithird_party/flashdb/inc -Ithird_party/flashdb/port/fal/inc -Icomponents/cfgmodel/include '
         '-Icomponents/proto/include -Ithird_party/cjson test/host/test_store.c test/host/nor_sim.c '
         'components/store/store.c components/store/fal_nor.c components/cfgmodel/cfgmodel.c components/proto/ipacl.c '
         'components/proto/jsonscan.c third_party/cjson/cJSON.c ' + FDB + ' -lm -o ' + EXE)
BUILD['store'] = STORE
BUILD['store_small'] = STORE.replace('test/host/test_store.c', '-DSTORE_CACHE_SIZE=3800u test/host/test_store.c')
# test_bind reads the records tools/test_provision.py signs (main() writes them first).
SAMPLE = os.path.join(FW, 'test/host/build/bind_sample.bin')
ARGS = {'install': [], 'bind': [SAMPLE], 'store': [], 'store_small': []}
# Extra environments to run the test with: a mutant is caught when any run fails.
RUNS = {'store': [{}, {'STORE_CUTS': '1'}]}
# (file, original text, mutated text, what the mutant breaks[, test: 'install' by default])
M = [
 ('boot/install.c', '        if (p == 0)\n            memset(buf, 0xFF, sizeof IMG_MAGIC);  // the magic is programmed last, on its own\n', '', 'magic programmed with its page'),
 ('boot/install.c', '    if (img_check(dst, dof, in->kick, &back) != IMG_OK || memcmp(back.raw, h->raw, IMG_CORE))\n        return -1;\n', '    (void)back;\n', 'copy not checked'),
 ('boot/install.c', '        if (img_check(in->spi, SPI_FACTORY, in->kick, &hf) != IMG_OK)\n            return halt(in, WHY_FACTORY_BAD);', '        img_check(in->spi, SPI_FACTORY, in->kick, &hf);', 'Reload skips the factory check'),
 ('boot/install.c', "        if (in->spi->erase_sector(in->spi, SPI_STAGING) || !staging_gone(in))  // never installed again\n            return halt(in, WHY_STAGING);\n", '        (void)staging_gone;\n', 'Reload leaves staging'),
 ('boot/install.c', "        if (in->spi->erase_sector(in->spi, SPI_STAGING) || !staging_gone(in))  // never installed again\n            return halt(in, WHY_STAGING);\n        if (copy(in, in->spi, SPI_FACTORY, &hf, in->qspi, QSPI_SLOT))\n            return halt(in, WHY_COPY);\n", "        if (copy(in, in->spi, SPI_FACTORY, &hf, in->qspi, QSPI_SLOT))\n            return halt(in, WHY_COPY);\n        if (in->spi->erase_sector(in->spi, SPI_STAGING) || !staging_gone(in))  // never installed again\n            return halt(in, WHY_STAGING);\n", 'staging erased after the restore'),
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
 ('components/ota/ota_app.c', '    if (spi->erase_sector(spi, SPI_STAGING) || spi->read(spi, SPI_STAGING, m, sizeof m) || memcmp(m, erased, sizeof m))\n        return fail(st, OTA_EFLASH);\n    st->erased = NOR_SECTOR;\n', '    (void)m;\n    (void)erased;\n', 'upload leaves the old staged image'),
 ('components/ota/bind.c', '    return crypto_ed25519_check(rec + 16, pub, msg, sizeof msg) == 0;', '    return true;', 'signature not checked', 'bind'),
 ('components/ota/bind.c', '    memcpy(msg + sizeof LABEL, uid, BIND_UID_LEN);', '    memcpy(msg + sizeof LABEL, rec + 4, BIND_UID_LEN);', 'signature over the stored UID', 'bind'),
 ('components/ota/bind.c', '    if (!crc_ok(rec))\n        return BIND_ECORRUPT;', '', 'corrupt record taken for a clone', 'bind'),
 ('components/ota/bind.c', '        qspi->erase_sector(qspi, QSPI_SLOT + s);', '        (void)s;', 'clone not erased', 'bind'),
 ('components/ota/bind.c', '    return BIND_CLONE;\n}', '    qspi->erase_sector(qspi, QSPI_BIND);\n    return BIND_CLONE;\n}', 'clone erases the record too', 'bind'),
 ('components/ota/bind.c', '        return BIND_ENONE;  // never provisioned', '        return BIND_OK;  // never provisioned', 'unprovisioned board runs', 'bind'),
 ('components/ota/bind.c', '    if (qspi->read(qspi, QSPI_BIND, rec, BIND_LEN))\n        return BIND_EREAD;', '    if (qspi->read(qspi, QSPI_BIND, rec, BIND_LEN))\n        memset(rec, 0, BIND_LEN);', 'read error taken for a record', 'bind'),
 ('boot/install.c', ' || !staging_gone(in))  // never installed again', ' || (0 && staging_gone(in)))  // never installed again', 'staging erase not verified'),
 ('components/ota/ota_app.c', ' || spi->read(spi, SPI_STAGING, m, sizeof m) || memcmp(m, erased, sizeof m))', ' || (0 && spi->read(spi, SPI_STAGING, m, sizeof m) && memcmp(m, erased, sizeof m)))', 'upload erase not verified'),
 ('components/store/store.c', '    if (s_gen)\n        delete_records(0, true);\n    else if (has_records())', '    delete_records(0, true);\n    if (has_records())', 'boot deletes records without a manifest', 'store'),
 ('components/store/store.c', 'static bool kept(uint32_t g) { return g == s_gen || pin_find(g) >= 0; }', 'static bool kept(uint32_t g) { return g == s_gen; }', 'pinned generation not kept', 'store'),
 ('components/store/store.c', '    if (r == CFG_OK && !kv_write(&s_edge_db, EDGE, manifest, strlen(manifest)))  // the commit point\n        r = CFG_ABORTED;\n', '', 'no manifest commit', 'store'),
 ('components/store/store.c', '    delete_records(0, true);      // the old generation unless pinned, and any stray record\n', '', 'old generation never deleted', 'store'),
 ('components/store/store.c', '    if (e != STORE_OK) {\n        delete_records(w.gen, false);', '    if (e != STORE_OK) {', 'failed save leaves its records', 'store'),
 ('components/store/store.c', '            if (s_cache[k].off > s_cache[i].off)\n                s_cache[k].off -= s_cache[i].len;\n', '', 'cache offsets not moved', 'store'),
 ('components/store/store.c', '    return s_used - (i >= 0 ? s_cache[i].len : 0) + len <= sizeof s_pool;', '    return s_used + len <= sizeof s_pool;', 'cache counts the old text too', 'store_small'),
 ('components/store/store.c', '            doc = cfg_load(&s_env, name, parsed, err, sizeof err);', '            doc = cJSON_Duplicate(parsed, true);', 'no re-validation at boot', 'store'),
 ('components/store/store.c', '        delete_records(p->gen, false);  // its slot is free now, so kept() no longer holds it', '        ;', 'doomed generation kept after unpin', 'store'),
 ('third_party/flashdb/src/fdb_kvdb.c', '    if (rescan) { /* SBIOT patch, see check_and_recovery_kv_cb */\n        goto __retry;\n    }\n', '', 'FlashDB recovery stops early', 'store'),
 ('components/store/store.c', '    UNLOCK();\n        bool more = b->n == LOG_BATCH;', '        bool more = b->n == LOG_BATCH;', 'log callback under the lock', 'store')
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
                kind = test[0] if test else 'install'
                fail = False
                for extra in RUNS.get(kind, [{}]):
                    env = dict(os.environ, OTA_QUICK='1', ASAN_OPTIONS='detect_leaks=0', **extra)
                    r = subprocess.run([EXE] + ARGS[kind], capture_output=True, text=True, errors='replace', timeout=600, env=env)
                    fail = fail or r.returncode != 0
                res = 'caught' if fail else 'SURVIVED'
                caught += fail
        finally:
            open(path, 'w').write(src)
        print('%-36s %s' % (what, res))
    print('mutants: %d of %d caught' % (caught, len(M)))
    return 0 if caught == len(M) else 1


if __name__ == '__main__':
    sys.exit(main())
