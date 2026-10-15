#!/usr/bin/env python3
"""Mutation check of the OTA logic: each mutant must make test_install fail.

    python3 stm32/firmware/test/host/mutants_ota.py      (from the repository root)

Builds test/host/test_install.c once per mutant with the host flags and runs it quickly
(OTA_QUICK=1 skips the two power-cut matrices). Every source file is restored afterwards.
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
 ("boot/install.c", "if (r->trial + 1 > 3) {", "if (r->trial + 1 > 4) {", "4 trials"),
 ("boot/install.c", "            enter_restore(in, RSN_TRIAL_IMAGE_BAD);  // never jump into a bad image, even in TRIAL\n            break;", "            ;", "TRIAL runs a bad image"),
 ("boot/install.c", "        if (!vs(in)) {\n            set_state(in, BR_CONFIRMED, RSN_STAGING_BAD);", "        if (0) {\n            set_state(in, BR_CONFIRMED, RSN_STAGING_BAD);", "PENDING skips the staging check"),
 ("boot/install.c", "            if (!vb(in)) {  // checked before QSPI is erased", "            if (0) {  // checked before QSPI is erased", "RESTORE skips the backup check"),
 ("boot/install.c", "        in->rec.attempts++;\n", "", "attempts not counted"),
 ("boot/install.c", "    if (vq(in)) {\n        in->rows |= INSTALL_ROW(4);", "    if (0) {\n        in->rows |= INSTALL_ROW(4);", "no row 4"),
 ("boot/install.c", "        in->ho->ram_fail++;  // counted before anything else", "        (void)0;", "ram_fail not counted"),
 ("boot/install.c", "        if (memcmp(buf, back, n))\n            return COPY_MISMATCH;", "", "no read-back compare"),
 ("boot/install.c", "        if (vb(in) && !memcmp(in->hb.raw, r->backup, IMG_CORE)) {", "        if (1) {", "BACKUP not verified"),
 ("boot/install.c", "        if (vq(in) && !memcmp(in->hq.raw, r->staging, IMG_CORE)) {", "        if (1) {", "VERIFY passes everything"),
 ("boot/install.c", "        if (r->state == BR_WRITE && exhausted && vb(in)) {", "        if (0) {", "no row 8"),
 ("components/ota/bootrec.c", "    return (int32_t)(a - b) > 0;", "    return a > b;", "seq without wrap-around"),
 ("components/ota/bootrec.c", "        e = spi->program(spi, slot + NOR_PAGE, b + NOR_PAGE, NOR_PAGE);  // the CRC page last", "        e = 0;", "CRC page never written"),
 ("components/ota/ota_image.c", "    if (memcmp(sha, hdr.sha256, 32))\n        return IMG_ESHA;", "", "SHA not checked"),
 ("components/ota/ota_app.c", "    if (ho->degraded || (rr == BOOTREC_VALID && r.state != BR_CONFIRMED))", "    if (0 && (ho->degraded || (rr == BOOTREC_VALID && r.state != BR_CONFIRMED)))", "OTA not refused in TRIAL"),
 ("components/ota/ota_app.c", "    if (memcmp(sha, st->hdr.sha256, 32))\n        return fail(st, OTA_EIMAGE);", "", "stage SHA not checked"),
 ("boot/install.c", "    } else if (vq(in)) {\n        set_state(in, BR_CONFIRMED, RSN_RESCUE_RETRY);", "    } else if (0) {\n        set_state(in, BR_CONFIRMED, RSN_RESCUE_RETRY);", "rescue retry halts with QSPI good"),
 ("boot/install.c", "    if (!put(in))\n        return rec_failed(in);\n    return decide(in);\n}", "    if (!put(in))\n        return ram_path(in);\n    return decide(in);\n}", "rescue retry skips rows 3-4"),
 ("boot/install.c", "        if (e)\n            return e;  // a read timeout stays a timeout (row 2)", "        if (e)\n            return NOR_EIO;", "read timeout becomes EIO"),
 ("boot/install.c", "    if (rr < 0)\n        return rec_failed(in);  // the record cannot be read: rows 3-6, never \"no record\"", "    if (rr < 0)\n        rr = BOOTREC_NONE;", "unreadable record = no record"),

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
