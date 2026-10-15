// Host tests for the bootloader's install logic (spec §11.2, §12.1): make -C stm32/firmware/test/host
// Simulated QSPI and SPI4 (nor_sim), power cuts at every erase and program, deadlines, failing
// record sectors. Invariant checked on every boot: the bootloader never jumps into an image that
// fails img_check.
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flashmap.h"
#include "install.h"
#include "nor_sim.h"
#include "ota_app.h"
#include "sha256.h"

static int failures;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

#define DEV_QSPI 1
#define DEV_SPI 2
#define BODY_V1 70000u  // 2 checkpoints with the header
#define BODY_V2 90000u

static uint32_t g_rows;  // decision-table rows seen over all tests

// ---- images -------------------------------------------------------------------------------

static uint8_t *make_image(uint16_t ver, uint32_t body, uint32_t *len)
{
    *len = IMG_HDR_SIZE + body;
    uint8_t *img = malloc(*len);
    memset(img, 0xFF, IMG_HDR_SIZE);
    uint8_t *b = img + IMG_HDR_SIZE;
    uint32_t x = ver * 2654435761u + body;
    for (uint32_t i = 0; i < body; i++) {
        x = x * 1664525u + 1013904223u;
        b[i] = (uint8_t)(x >> 24);
    }
    uint32_t msp = 0x20020000u, reset = QSPI_XIP_BASE + IMG_HDR_SIZE + (body > 0x200 ? 0x1E9u : 1u);
    memcpy(b, &msp, 4);
    memcpy(b + 4, &reset, 4);
    uint8_t sha[32];
    sha256_t s;
    sha256_init(&s);
    sha256_update(&s, b, body);
    sha256_final(&s, sha);
    uint16_t v[3] = {ver, 0, 0};
    img_make(img, v, body, sha);
    return img;
}

static void put_image(nor_sim_t *n, uint32_t off, uint16_t ver, uint32_t body)
{
    uint32_t len;
    uint8_t *img = make_image(ver, body, &len);
    memcpy(n->mem + off, img, len);
    free(img);
}

static void corrupt(nor_sim_t *n, uint32_t off)
{
    n->mem[off] ^= 0x5A;
}

static uint16_t version_at(nor_sim_t *n, uint32_t off)
{
    img_hdr_t h;
    return img_check(&n->nor, off, NULL, &h) == IMG_OK ? h.version[0] : 0;
}

// Damages the image at img_off: one body byte (the header's 0xFF padding is not checked by
// anything). When an image was there, it must now fail its check, or the test proves nothing.
static void damage(nor_sim_t *n, uint32_t img_off)
{
    bool was = version_at(n, img_off) != 0;
    n->mem[img_off + IMG_HDR_SIZE + 0x100] ^= 0x5A;
    if (was)
        CHECK(version_at(n, img_off) == 0, "damage at 0x%x left the image valid", (unsigned)img_off);
}

// ---- the bench ----------------------------------------------------------------------------

typedef struct {
    sim_world_t w;
    nor_sim_t q, s;
    handoff_t ho;
    jmp_buf jb;
    int boots, cuts, retries, trial_runs;
    uint32_t rows;
    uint8_t *upload_img;  // owned here: a power cut can longjmp out of upload()
} bench_t;

static void bench_init(bench_t *b, uint32_t seed)
{
    memset(b, 0, sizeof *b);
    sim_world_init(&b->w, seed);
    b->w.cut_jmp = &b->jb;
    nor_sim_init(&b->q, &b->w, DEV_QSPI, 0x400000);
    nor_sim_init(&b->s, &b->w, DEV_SPI, 0x400000);
    memset(&b->ho, 0xA5, sizeof b->ho);  // first power-on: garbage
}

static void bench_free(bench_t *b)
{
    g_rows |= b->rows;
    free(b->upload_img);
    nor_sim_free(&b->q);
    nor_sim_free(&b->s);
}

static void record(bench_t *b, uint8_t state, uint8_t offset, uint8_t attempts, uint8_t verify_fail, uint8_t trial,
                   uint16_t staging_ver, uint32_t staging_body, uint16_t backup_ver, uint32_t backup_body)
{
    bootrec_t r;
    memset(&r, 0, sizeof r);
    r.state = state;
    r.offset = offset;
    r.attempts = attempts;
    r.verify_fail = verify_fail;
    r.trial = trial;
    uint32_t len;
    if (staging_ver) {
        uint8_t *img = make_image(staging_ver, staging_body, &len);
        memcpy(r.staging, img, IMG_CORE);
        free(img);
    }
    if (backup_ver) {
        uint8_t *img = make_image(backup_ver, backup_body, &len);
        memcpy(r.backup, img, IMG_CORE);
        free(img);
    }
    long saved = b->w.ops;
    CHECK(bootrec_write(&b->s.nor, &r) == NOR_OK, "test record written");
    b->w.ops = saved;  // set-up writes do not count
}

// Uploads an image through the app's staging API (as POST /api/ota would). Returns the result.
static int upload(bench_t *b, uint16_t ver, uint32_t body)
{
    uint32_t len;
    free(b->upload_img);
    uint8_t *img = b->upload_img = make_image(ver, body, &len);
    ota_stage_t st;
    handoff_t ho = b->ho;
    handoff_load(&ho);
    int r = ota_stage_begin(&st, &b->s.nor, &ho, len);
    for (uint32_t i = 0; !r && i < len; i += 1000)
        r = ota_stage_write(&st, img + i, len - i < 1000 ? len - i : 1000);
    if (!r)
        r = ota_stage_finish(&st);
    return r;
}

enum { APP_CONFIRM, APP_CRASH, APP_ROLLBACK };
enum { OUT_RUN = 1, OUT_HALT, OUT_STUCK };

typedef struct {
    int kind;
    uint16_t version;
    bool degraded;
    uint8_t state;
    int cuts;  // power cuts that happened (one_run)
} outcome_t;

// Boots until the app runs with nothing left to do, or the board stays in halted rescue. A power
// cut (longjmp out of any erase/program) loses the handoff block. A rescue retry is the 60 s pause.
static outcome_t boot_until_stable(bench_t *b, int policy, int max_boots)
{
    outcome_t o = {0};
    if (setjmp(b->jb)) {
        b->cuts++;
        memset(&b->ho, 0xA5, sizeof b->ho);  // power cut: backup SRAM lost
    }
    while (b->boots++ < max_boots) {
        handoff_load(&b->ho);
        install_t in = {.qspi = &b->q.nor, .spi = &b->s.nor, .ho = &b->ho};
        install_result_t r = install_run(&in);
        b->rows |= in.rows;
        while (r == INSTALL_RESCUE_RETRY && b->retries++ < 20) {
            r = install_rescue_retry(&in);
            b->rows |= in.rows;
        }
        CHECK(!(in.rows & INSTALL_BUG), "decision loop did not settle (rows %08x)", (unsigned)in.rows);
        if (r == INSTALL_RESCUE_RETRY) {
            o.kind = OUT_STUCK;
            return o;
        }
        if (r == INSTALL_RESCUE_HALT) {
            o.kind = OUT_HALT;
            return o;
        }
        if (r != INSTALL_JUMP)
            continue;  // REBOOT or WATCHDOG
        img_hdr_t h;
        int ok = img_check(&b->q.nor, QSPI_SLOT, NULL, &h);
        CHECK(ok == IMG_OK, "jumped into an image that fails its check (%d)", ok);
        if (ok != IMG_OK) {
            o.kind = OUT_STUCK;
            return o;
        }
        bootrec_t rec;
        bool have = bootrec_read(&b->s.nor, &rec) == BOOTREC_VALID;
        if (have && rec.state == BR_TRIAL && !in.degraded) {
            b->trial_runs++;
            if (policy == APP_CRASH)
                continue;  // the new image hangs before confirming: IWDG1 resets
            if (policy == APP_ROLLBACK) {
                ota_rollback(&b->s.nor);
                continue;
            }
            ota_confirm(&b->s.nor);
            have = bootrec_read(&b->s.nor, &rec) == BOOTREC_VALID;
        }
        o.kind = OUT_RUN;
        o.version = h.version[0];
        o.degraded = in.degraded;
        o.state = have ? rec.state : 0;
        return o;
    }
    o.kind = OUT_STUCK;
    return o;
}

// ---- plain cases ----------------------------------------------------------------------------

static void test_first_boot_and_blank(void)
{
    bench_t b;
    bench_init(&b, 1);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    outcome_t o = boot_until_stable(&b, APP_CONFIRM, 5);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.state == BR_CONFIRMED, "first boot after SWD: v1 confirmed");
    bench_free(&b);

    bench_init(&b, 2);
    o = boot_until_stable(&b, APP_CONFIRM, 5);
    CHECK(o.kind == OUT_HALT, "blank flash: halted rescue (row 1)");
    bench_free(&b);

    bench_init(&b, 3);  // no record, QSPI bad, backup good
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    damage(&b.q, QSPI_SLOT);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.state == BR_CONFIRMED, "no record, bad QSPI: restored from backup");
    bench_free(&b);

    bench_init(&b, 4);  // no record, only staging good
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_RUN && o.version == 2 && o.state == BR_CONFIRMED, "no record, only staging: installed through rescue retry");
    bench_free(&b);
}

static void test_records(void)
{
    bench_t b;
    bench_init(&b, 5);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    // seq wrap-around: a record with seq 0xFFFFFFFF, then the next write gets seq 0 and must win.
    bootrec_t r;
    memset(&r, 0, sizeof r);
    r.state = BR_CONFIRMED;
    uint8_t enc[512];
    r.seq = 0xFFFFFFFFu;
    bootrec_encode(&r, enc);
    memcpy(b.s.mem + SPI_REC_A, enc, sizeof enc);
    r.state = BR_TRIAL;
    CHECK(bootrec_write(&b.s.nor, &r) == NOR_OK && r.seq == 0, "seq wraps to 0");
    bootrec_t got;
    CHECK(bootrec_read(&b.s.nor, &got) == BOOTREC_VALID && got.state == BR_TRIAL && got.seq == 0, "seq 0 is newer than 0xFFFFFFFF");
    // both copies damaged: treated as no record
    corrupt(&b.s, SPI_REC_A + 10);
    corrupt(&b.s, SPI_REC_B + 10);
    CHECK(bootrec_read(&b.s.nor, &got) == BOOTREC_NONE, "both records damaged: none valid");
    outcome_t o = boot_until_stable(&b, APP_CONFIRM, 5);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.state == BR_CONFIRMED, "both records damaged: QSPI image confirmed");
    bench_free(&b);
}

static void test_handoff(void)
{
    handoff_t h;
    memset(&h, 0x3C, sizeof h);
    handoff_load(&h);
    CHECK(h.rec_fail == 0 && h.ram_fail == 0 && h.degraded == 0, "handoff with a bad CRC starts from zero");
    h.rec_fail = 2;
    handoff_store(&h);
    handoff_load(&h);
    CHECK(h.rec_fail == 2, "handoff survives a reset with its CRC");
    h.ram_fail = 1;  // changed without handoff_store: the magic is right, the CRC is not
    handoff_load(&h);
    CHECK(h.magic != 0 && h.rec_fail == 0 && h.ram_fail == 0, "handoff with the right magic but a wrong CRC starts from zero");
}

static void test_image_sizes(void)
{
    uint32_t sizes[2] = {8, IMG_MAX_BODY};
    for (int i = 0; i < 2; i++) {
        bench_t b;
        bench_init(&b, 10 + i);
        put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
        boot_until_stable(&b, APP_CONFIRM, 3);
        CHECK(upload(&b, 2, sizes[i]) == OTA_OK, "upload of a %u-byte body", (unsigned)sizes[i]);
        outcome_t o = boot_until_stable(&b, APP_CONFIRM, 10);
        CHECK(o.kind == OUT_RUN && o.version == 2 && o.state == BR_CONFIRMED, "install of a %u-byte body", (unsigned)sizes[i]);
        bench_free(&b);
    }
}

// ---- the state machine paths ------------------------------------------------------------------

static void test_staging_bad(void)
{
    bench_t b;
    bench_init(&b, 20);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    damage(&b.s, SPI_STAGING);
    record(&b, BR_PENDING, 0, 0, 0, 0, 2, BODY_V2, 0, 0);
    outcome_t o = boot_until_stable(&b, APP_CONFIRM, 5);
    bootrec_t r;
    bootrec_read(&b.s.nor, &r);
    CHECK(o.kind == OUT_RUN && o.version == 1 && r.reason == RSN_STAGING_BAD, "bad staging: v1 kept, reason recorded");
    CHECK(version_at(&b.s, SPI_BACKUP) == 0, "bad staging: nothing was copied");
    bench_free(&b);
}

static void test_trial(void)
{
    bench_t b;
    bench_init(&b, 30);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);
    CHECK(upload(&b, 2, BODY_V2) == OTA_OK, "upload v2");
    outcome_t o = boot_until_stable(&b, APP_CRASH, 20);
    bootrec_t r;
    bootrec_read(&b.s.nor, &r);
    CHECK(o.kind == OUT_RUN && o.version == 1 && r.state == BR_CONFIRMED && b.trial_runs == 3,
          "v2 never confirms: v1 back after exactly 3 trial boots (%d)", b.trial_runs);
    bench_free(&b);

    bench_init(&b, 31);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);
    upload(&b, 2, BODY_V2);
    o = boot_until_stable(&b, APP_ROLLBACK, 20);
    CHECK(o.kind == OUT_RUN && o.version == 1, "v2 asks for rollback: v1 back");
    bench_free(&b);

    bench_init(&b, 32);  // TRIAL, but the new image in QSPI is damaged: must not run it
    put_image(&b.q, QSPI_SLOT, 2, BODY_V2);
    damage(&b.q, QSPI_SLOT);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_TRIAL, 0, 0, 0, 1, 2, BODY_V2, 1, BODY_V1);
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_RUN && o.version == 1, "damaged image in TRIAL: restored, never run");
    bench_free(&b);

    bench_init(&b, 33);  // ota_stage refuses while TRIAL
    put_image(&b.q, QSPI_SLOT, 2, BODY_V2);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_TRIAL, 0, 0, 0, 1, 2, BODY_V2, 1, BODY_V1);
    CHECK(upload(&b, 3, BODY_V1) == OTA_EBUSY, "upload during TRIAL: 409");
    bench_free(&b);
}

static void test_verify_and_restore_checks(void)
{
    bench_t b;
    bench_init(&b, 40);  // VERIFY finds the QSPI image wrong once: WRITE again
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    put_image(&b.q, QSPI_SLOT, 2, BODY_V2);
    damage(&b.q, QSPI_SLOT);
    record(&b, BR_VERIFY, 0, 0, 0, 0, 2, BODY_V2, 1, BODY_V1);
    outcome_t o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_RUN && o.version == 2 && o.state == BR_CONFIRMED, "VERIFY mismatch: written again, then installed");
    bench_free(&b);

    bench_init(&b, 41);  // RESTORE finished but the QSPI SHA is wrong: RESTORE(0) again
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    damage(&b.q, QSPI_SLOT);
    record(&b, BR_RESTORE, 2, 0, 0, 0, 2, BODY_V2, 1, BODY_V1);
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    bootrec_t r;
    bootrec_read(&b.s.nor, &r);
    CHECK(o.kind == OUT_RUN && o.version == 1 && r.reason == RSN_RESTORED, "RESTORE done with a bad SHA: restored again");
    bench_free(&b);

    bench_init(&b, 42);  // RESTORE, backup bad from the start, QSPI still good: keep running it
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    record(&b, BR_RESTORE, 0, 0, 0, 0, 0, 0, 1, BODY_V1);
    o = boot_until_stable(&b, APP_CONFIRM, 5);
    bootrec_read(&b.s.nor, &r);
    CHECK(o.kind == OUT_RUN && o.version == 1 && r.reason == RSN_BACKUP_BAD, "RESTORE without a backup: QSPI kept");
    bench_free(&b);
}

static void test_exhausted(void)
{
    bench_t b;
    outcome_t o;
    bench_init(&b, 50);  // row 7: BACKUP attempts > 3 -> install cancelled, v1 runs
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    record(&b, BR_BACKUP, 0, 4, 0, 0, 2, BODY_V2, 1, BODY_V1);
    o = boot_until_stable(&b, APP_CONFIRM, 5);
    CHECK(o.kind == OUT_RUN && o.version == 1 && (b.rows & INSTALL_ROW(7)), "row 7: BACKUP exhausted, v1 kept");
    bench_free(&b);

    bench_init(&b, 51);  // row 8: WRITE attempts > 3 with a backup -> RESTORE
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_WRITE, 1, 4, 0, 0, 2, BODY_V2, 1, BODY_V1);
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_RUN && o.version == 1 && (b.rows & INSTALL_ROW(8)), "row 8: WRITE exhausted, backup restored");
    bench_free(&b);

    bench_init(&b, 52);  // row 8 by verify_fail
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_WRITE, 0, 0, 2, 0, 2, BODY_V2, 1, BODY_V1);
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_RUN && o.version == 1, "row 8: two failed VERIFYs, backup restored");
    bench_free(&b);

    bench_init(&b, 53);  // row 9: WRITE exhausted, no backup -> rescue retry from staging
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    record(&b, BR_WRITE, 1, 4, 0, 0, 2, BODY_V2, 0, 0);
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_RUN && o.version == 2 && (b.rows & INSTALL_ROW(9)), "row 9: retried from staging");
    bench_free(&b);

    bench_init(&b, 54);  // row 9: RESTORE exhausted -> rescue retry, then restored
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_RESTORE, 1, 4, 0, 0, 0, 0, 1, BODY_V1);
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_RUN && o.version == 1 && (b.rows & INSTALL_ROW(9)), "row 9: RESTORE retried after the pause");
    bench_free(&b);

    bench_init(&b, 55);  // WRITE exhausted, QSPI, backup and staging all bad: halt, no retry (row 1)
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    damage(&b.s, SPI_STAGING);
    record(&b, BR_WRITE, 1, 4, 0, 0, 2, BODY_V2, 0, 0);
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_HALT && b.retries == 0, "nothing valid left: halted rescue without retries");
    bench_free(&b);
}

// ---- flash faults ---------------------------------------------------------------------------

#define REC_LO SPI_REC_A
#define REC_HI (SPI_REC_B + NOR_SECTOR)

static void clear_faults(sim_world_t *w, void *arg)
{
    (void)arg;
    sim_fail(w, 0, 0, 0, 0, 0);
    sim_fail(w, 1, 0, 0, 0, 0);
}

static void clear_qspi_fault(sim_world_t *w, void *arg)
{
    (void)arg;
    sim_fail(w, 1, 0, 0, 0, 0);
}

static void start_handoff(bench_t *b, uint8_t rec_fail)
{
    handoff_load(&b->ho);
    b->ho.rec_fail = rec_fail;
    handoff_store(&b->ho);
}

static void test_deadlines(void)
{
    // A timeout in the middle of WRITE: row 2 (watchdog), counted, then the install finishes.
    bench_t b;
    bench_init(&b, 60);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);
    upload(&b, 2, BODY_V2);
    b.w.timeout_at = b.w.ops + 500;
    outcome_t o = boot_until_stable(&b, APP_CONFIRM, 20);
    CHECK(o.kind == OUT_RUN && o.version == 2 && (b.rows & INSTALL_ROW(2)), "a missed deadline: watchdog reset, install finished");
    bench_free(&b);

    // QSPI erases always time out during WRITE: attempts run out, RESTORE fails too, rescue retries
    // until the fault goes away, then v1 is restored. Never a jump into a half image.
    bench_init(&b, 61);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);
    upload(&b, 2, BODY_V2);
    sim_fail(&b.w, 0, DEV_QSPI, 0x10000, 0x20000, NOR_TIMEOUT);  // the second checkpoint hangs
    b.w.hook = clear_faults;
    b.w.hook_at = b.w.ops + 3000;
    o = boot_until_stable(&b, APP_CONFIRM, 60);
    CHECK(o.kind == OUT_RUN && o.version == 1 && (b.rows & INSTALL_ROW(2)) && (b.rows & INSTALL_ROW(8)),
          "QSPI hangs in WRITE: attempts run out (row 8), v1 restored once QSPI works (v%u rows %x)", o.version,
          (unsigned)b.rows);
    bench_free(&b);
}

// Hook before the last page of the copy: from now on the records fail, and the copy's source and
// the other slot are damaged (they were read already for every page but this last one's source).
static void break_everything_but_qspi(sim_world_t *w, void *arg)
{
    struct last { bench_t *b; uint32_t src; } *a = arg;
    sim_fail(w, 0, DEV_SPI, REC_LO, REC_HI, NOR_EIO);
    damage(&a->b->s, a->src);
    damage(&a->b->s, a->src == SPI_STAGING ? SPI_BACKUP : SPI_STAGING);
}

static void arm_backup_read_timeout(sim_world_t *w, void *arg)
{
    (void)arg;
    sim_fail(w, 1, DEV_SPI, SPI_BACKUP + 0x8000, SPI_BACKUP + 0x8100, SIM_READ_TIMEOUT);
}

static void test_record_failures(void)
{
    bench_t b;
    outcome_t o;
    // Row 4: records cannot be written, QSPI valid: run degraded, nothing erased.
    bench_init(&b, 70);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);
    sim_fail(&b.w, 0, DEV_SPI, REC_LO, REC_HI, NOR_EIO);
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    o = boot_until_stable(&b, APP_CONFIRM, 3);
    CHECK(o.kind == OUT_RUN && o.version == 1, "record not writable, no change needed: v1 runs");
    {
        // a PENDING written before the failure starts is postponed, and OTA is refused (409)
        bench_t c;
        bench_init(&c, 71);
        put_image(&c.q, QSPI_SLOT, 1, BODY_V1);
        boot_until_stable(&c, APP_CONFIRM, 3);
        upload(&c, 2, BODY_V2);
        sim_fail(&c.w, 0, DEV_SPI, REC_LO, REC_HI, NOR_EIO);
        o = boot_until_stable(&c, APP_CONFIRM, 3);
        CHECK(o.kind == OUT_RUN && o.version == 1 && o.degraded && (c.rows & INSTALL_ROW(4)), "row 4: degraded, install postponed");
        CHECK(c.ho.degraded == 1, "row 4: the app sees degraded");
        CHECK(upload(&c, 3, BODY_V1) == OTA_EBUSY, "row 4: OTA refused while degraded");
        bench_free(&c);
    }
    bench_free(&b);

    // Rows 5 then 6: during WRITE (QSPI half written) the records start failing; after 3 failed
    // boots the RAM-tracked path restores the backup and runs it degraded.
    bench_init(&b, 72);
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    put_image(&b.q, QSPI_SLOT, 2, BODY_V2);
    corrupt(&b.q, 0x10010);  // half written
    record(&b, BR_WRITE, 1, 1, 0, 0, 2, BODY_V2, 1, BODY_V1);
    sim_fail(&b.w, 0, DEV_SPI, REC_LO, REC_HI, NOR_TIMEOUT);  // a record timeout is row 3, not row 2
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.degraded && (b.rows & INSTALL_ROW(5)) && (b.rows & INSTALL_ROW(6)),
          "rows 5, 6: RAM-tracked restore of the backup, degraded (v%u rows %x)", o.version, (unsigned)b.rows);
    bench_free(&b);

    // Row 6 with a bad backup: falls back to staging (step 4 drops the bad source).
    bench_init(&b, 73);
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    damage(&b.s, SPI_BACKUP);
    record(&b, BR_WRITE, 1, 1, 0, 0, 2, BODY_V2, 1, BODY_V1);
    sim_fail(&b.w, 0, DEV_SPI, REC_LO, REC_HI, NOR_EIO);
    start_handoff(&b, 2);
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_RUN && o.version == 2 && o.degraded, "row 6: bad backup skipped, staging copied");
    bench_free(&b);

    // Row 6, the RAM copy misses its deadline: watchdog, counted in ram_fail; after 3 the rescue
    // retry takes over; once QSPI works again the recovery succeeds. Records fail throughout.
    bench_init(&b, 74);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_RESTORE, 1, 1, 0, 0, 0, 0, 1, BODY_V1);
    start_handoff(&b, 2);
    sim_fail(&b.w, 0, DEV_SPI, REC_LO, REC_HI, NOR_EIO);
    sim_fail(&b.w, 1, DEV_QSPI, 0, 0x400000, NOR_TIMEOUT);
    b.w.hook = clear_qspi_fault;
    b.w.hook_at = b.w.ops + 12;  // two operations per boot: about five boots of hanging QSPI
    o = boot_until_stable(&b, APP_CONFIRM, 20);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.degraded && (b.rows & INSTALL_ROW(2)) && b.retries > 0,
          "row 6: deadline, ram_fail > 3, rescue retry, then restored (v%u rows %x retries %d)", o.version,
          (unsigned)b.rows, b.retries);
    bench_free(&b);

    // Row 6, power lost in the middle of the RAM copy: the handoff block goes with it (rec_fail and
    // ram_fail back to 0), and the next boots work their way to the RAM path again.
    bench_init(&b, 76);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_RESTORE, 1, 1, 0, 0, 0, 0, 1, BODY_V1);
    start_handoff(&b, 2);
    sim_fail(&b.w, 0, DEV_SPI, REC_LO, REC_HI, NOR_EIO);
    b.w.cut_at = b.w.ops + 30;  // inside the RAM copy (after the failed record write)
    o = boot_until_stable(&b, APP_CONFIRM, 20);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.degraded, "row 6: power cut during the RAM copy, then restored");
    bench_free(&b);

    // Row 4, special case: WRITE copies its last checkpoint, then the record update fails, while
    // staging and backup are both damaged. QSPI holds the only valid image: run it degraded, do not
    // erase it, do not halt. Same for RESTORE.
    for (int restore = 0; restore < 2; restore++) {
        bench_init(&b, 77 + restore);
        uint32_t src = restore ? SPI_BACKUP : SPI_STAGING;
        put_image(&b.s, src, 2, BODY_V2);
        put_image(&b.q, QSPI_SLOT, 2, BODY_V2);  // checkpoint 0 done ...
        corrupt(&b.q, 0x10100);                  // ... checkpoint 1 not yet
        if (restore)
            record(&b, BR_RESTORE, 1, 0, 0, 0, 0, 0, 2, BODY_V2);
        else
            record(&b, BR_WRITE, 1, 0, 0, 0, 2, BODY_V2, 0, 0);
        struct last { bench_t *b; uint32_t src; } arg = {&b, src};
        b.w.hook = break_everything_but_qspi;
        b.w.hook_arg = &arg;
        b.w.hook_dev = DEV_QSPI;
        b.w.hook_addr = (IMG_HDR_SIZE + BODY_V2 - 1) & ~(NOR_PAGE - 1);  // the last page of the image
        o = boot_until_stable(&b, APP_CONFIRM, 5);
        CHECK(o.kind == OUT_RUN && o.version == 2 && o.degraded && (b.rows & INSTALL_ROW(4)),
              "%s: last checkpoint done, record fails, only QSPI valid: degraded run", restore ? "RESTORE" : "WRITE");
        CHECK(version_at(&b.q, QSPI_SLOT) == 2, "and QSPI still holds the image");
        bench_free(&b);
    }
}

// ---- copies that go wrong without an error -------------------------------------------------------

static void corrupt_qspi_early(sim_world_t *w, void *arg)
{
    (void)w;
    damage(arg, QSPI_SLOT);  // already written and compared: only VERIFY can see it now
}

static void corrupt_backup_early(sim_world_t *w, void *arg)
{
    (void)w;
    damage(arg, SPI_BACKUP);  // already written and compared: only the BACKUP check sees it
}

static void test_silent_damage(void)
{
    bench_t b;
    outcome_t o;
    // Programming QSPI silently does nothing: the checkpoint's read-back must catch it at once
    // (a reset in WRITE), not let it run on to VERIFY.
    bench_init(&b, 80);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);
    upload(&b, 2, BODY_V2);
    sim_fail(&b.w, 0, DEV_QSPI, 0, 0x400000, SIM_SILENT);
    handoff_load(&b.ho);
    install_t in = {.qspi = &b.q.nor, .spi = &b.s.nor, .ho = &b.ho};
    install_result_t r = install_run(&in);
    CHECK(r == INSTALL_REBOOT && in.rec.state == BR_WRITE && in.rec.offset == 0 && in.rec.attempts == 1,
          "a checkpoint that does not read back: reset in WRITE (r %d state %u)", r, in.rec.state);
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    b.boots = 0;
    o = boot_until_stable(&b, APP_CONFIRM, 20);
    CHECK(o.kind == OUT_RUN && o.version == 2, "then the install finishes");
    bench_free(&b);

    // QSPI changes after WRITE compared it: VERIFY must catch it and WRITE again; v2 ends installed.
    bench_init(&b, 81);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);
    upload(&b, 2, BODY_V2);
    b.w.hook = corrupt_qspi_early;
    b.w.hook_arg = &b.q;
    b.w.hook_dev = DEV_QSPI;
    b.w.hook_addr = (IMG_HDR_SIZE + BODY_V2 - 1) & ~(NOR_PAGE - 1);
    o = boot_until_stable(&b, APP_CONFIRM, 20);
    CHECK(o.kind == OUT_RUN && o.version == 2 && o.state == BR_CONFIRMED, "QSPI damaged after its copy: VERIFY rewrites it (v%u)", o.version);
    bench_free(&b);

    // The backup changes after BACKUP compared it: the backup check must catch it and copy again, so
    // the backup is good at the end.
    bench_init(&b, 82);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);
    upload(&b, 2, BODY_V2);
    b.w.hook = corrupt_backup_early;
    b.w.hook_arg = &b.s;
    b.w.hook_dev = DEV_SPI;
    b.w.hook_addr = SPI_BACKUP + ((IMG_HDR_SIZE + BODY_V1 - 1) & ~(NOR_PAGE - 1));
    o = boot_until_stable(&b, APP_CONFIRM, 20);
    CHECK(o.kind == OUT_RUN && o.version == 2 && version_at(&b.s, SPI_BACKUP) == 1,
          "backup damaged after its copy: copied again, backup good at the end");
    bench_free(&b);
}

static void corrupt_qspi_early(sim_world_t *w, void *arg);

// ---- review findings (Codex r1 of Plan 04a) ----------------------------------------------------

static void test_read_faults(void)
{
    bench_t b;
    outcome_t o;
    // A read of the copy source times out: row 2 (watchdog), counted; then the install finishes.
    bench_init(&b, 100);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);
    upload(&b, 2, BODY_V2);
    record(&b, BR_WRITE, 0, 0, 0, 0, 2, BODY_V2, 1, BODY_V1);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    sim_fail(&b.w, 0, DEV_SPI, SPI_STAGING + 0x8000, SPI_STAGING + 0x8100, SIM_READ_TIMEOUT);
    handoff_load(&b.ho);
    install_t in = {.qspi = &b.q.nor, .spi = &b.s.nor, .ho = &b.ho};
    install_result_t r = install_run(&in);
    CHECK(r == INSTALL_WATCHDOG && (in.rows & INSTALL_ROW(2)), "a source read timeout in WRITE: watchdog (row 2), r %d", r);
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    b.boots = 0;
    o = boot_until_stable(&b, APP_CONFIRM, 20);
    CHECK(o.kind == OUT_RUN && o.version == 2, "then the install finishes");
    bench_free(&b);

    // The same in the RAM path (records not writable, QSPI half written).
    bench_init(&b, 101);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_RESTORE, 1, 1, 0, 0, 0, 0, 1, BODY_V1);
    start_handoff(&b, 2);
    sim_fail(&b.w, 0, DEV_SPI, REC_LO, REC_HI, NOR_EIO);
    sim_fail(&b.w, 1, DEV_SPI, SPI_BACKUP + 0x8000, SPI_BACKUP + 0x8100, SIM_READ_TIMEOUT);
    handoff_load(&b.ho);
    install_t in2 = {.qspi = &b.q.nor, .spi = &b.s.nor, .ho = &b.ho};
    // img_check of the backup reads 0x8000 too: give it a clean read first, then fail the copy's read
    sim_fail(&b.w, 1, 0, 0, 0, 0);
    b.w.hook = arm_backup_read_timeout;
    b.w.hook_arg = &b.w;
    b.w.hook_dev = DEV_QSPI;
    b.w.hook_addr = 0;  // the first QSPI erase of the RAM copy
    r = install_run(&in2);
    CHECK(r == INSTALL_WATCHDOG && (in2.rows & INSTALL_ROW(6)) && (in2.rows & INSTALL_ROW(2)),
          "a source read timeout in the RAM path: watchdog (rows 6, 2), r %d", r);
    bench_free(&b);
}

static void test_record_read_errors(void)
{
    bench_t b;
    bench_init(&b, 110);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);  // a valid record now exists
    bootrec_t before;
    CHECK(bootrec_read(&b.s.nor, &before) == BOOTREC_VALID, "record present");
    uint8_t a[512], c[512];
    memcpy(a, b.s.mem + SPI_REC_A, sizeof a);
    memcpy(c, b.s.mem + SPI_REC_B, sizeof c);
    sim_fail(&b.w, 0, DEV_SPI, SPI_REC_A, SPI_REC_A + NOR_SECTOR, SIM_READ_EIO);  // copy A unreadable
    bootrec_t r = before;
    CHECK(bootrec_read(&b.s.nor, &r) < 0, "an unreadable copy is an error, not 'no record'");
    CHECK(bootrec_write(&b.s.nor, &r) < 0 && !memcmp(a, b.s.mem + SPI_REC_A, sizeof a) &&
          !memcmp(c, b.s.mem + SPI_REC_B, sizeof c), "no record is written (or erased) while a copy cannot be read");
    outcome_t o = boot_until_stable(&b, APP_CONFIRM, 3);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.degraded && (b.rows & INSTALL_ROW(3)), "boot with an unreadable record: degraded run");
    CHECK(upload(&b, 2, BODY_V2) != OTA_OK, "no upload while the boot state is unknown");
    bench_free(&b);

    // A record read that fails once, in TRIAL: the boot must not take it for "no record" and write a
    // fresh CONFIRMED (that would confirm the new image behind the app's back).
    bench_init(&b, 111);
    put_image(&b.q, QSPI_SLOT, 2, BODY_V2);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_TRIAL, 0, 0, 0, 1, 2, BODY_V2, 1, BODY_V1);
    sim_fail(&b.w, 0, DEV_SPI, REC_LO, REC_HI, SIM_READ_EIO);
    b.w.fail[0].count = 1;
    handoff_load(&b.ho);
    install_t in = {.qspi = &b.q.nor, .spi = &b.s.nor, .ho = &b.ho};
    install_result_t res = install_run(&in);
    bootrec_t after;
    CHECK(res == INSTALL_JUMP && in.degraded && bootrec_read(&b.s.nor, &after) == BOOTREC_VALID && after.state == BR_TRIAL,
          "one failed record read in TRIAL: degraded run, TRIAL kept (state %u)", after.state);
    bench_free(&b);
}

static void test_rescue_retry_keeps_qspi(void)
{
    bench_t b;
    outcome_t o;
    // RESTORE exhausted, backup and staging bad, QSPI good: the retry must run QSPI, not halt.
    bench_init(&b, 120);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    record(&b, BR_RESTORE, 1, 4, 0, 0, 0, 0, 1, BODY_V1);
    o = boot_until_stable(&b, APP_CONFIRM, 5);
    CHECK(o.kind == OUT_RUN && o.version == 1 && b.retries > 0, "rescue retry without a source: QSPI runs (kind %d)", o.kind);
    bench_free(&b);

    // RESTORE exhausted, a valid backup, records not writable, QSPI good: the retry cannot write its
    // record, so rows 3-4 apply: run QSPI degraded, erase nothing.
    bench_init(&b, 121);
    put_image(&b.q, QSPI_SLOT, 2, BODY_V2);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_RESTORE, 0, 4, 0, 0, 0, 0, 1, BODY_V1);
    sim_fail(&b.w, 0, DEV_SPI, REC_LO, REC_HI, NOR_EIO);
    o = boot_until_stable(&b, APP_CONFIRM, 5);
    CHECK(o.kind == OUT_RUN && o.version == 2 && o.degraded && version_at(&b.q, QSPI_SLOT) == 2 && b.ho.rec_fail >= 1,
          "rescue retry with a failing record: QSPI kept, degraded, rec_fail counted");
    bench_free(&b);
}

static void test_ram_path_bad_result(void)
{
    // The RAM copy programs nothing (silently): the check of QSPI fails, so no jump; once QSPI
    // works the next RAM attempt succeeds.
    bench_t b;
    bench_init(&b, 130);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_RESTORE, 1, 1, 0, 0, 0, 0, 1, BODY_V1);
    start_handoff(&b, 2);
    sim_fail(&b.w, 0, DEV_SPI, REC_LO, REC_HI, NOR_EIO);
    sim_fail(&b.w, 1, DEV_QSPI, 0, 0x400000, SIM_SILENT);
    handoff_load(&b.ho);
    install_t in = {.qspi = &b.q.nor, .spi = &b.s.nor, .ho = &b.ho};
    install_result_t r = install_run(&in);
    CHECK(r == INSTALL_REBOOT && (in.rows & INSTALL_ROW(6)) && b.ho.ram_fail == 1,
          "RAM copy with a bad result: reboot, never a jump (r %d)", r);
    sim_fail(&b.w, 1, 0, 0, 0, 0);
    outcome_t o = boot_until_stable(&b, APP_CONFIRM, 5);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.degraded && b.ho.ram_fail == 0, "then restored, ram_fail back to 0");
    bench_free(&b);

    // The RAM copy completes, every page reads back right, but a body byte changes afterwards (the
    // hook fires before the last page, on a page already compared): the header is valid, the SHA is
    // not. No jump, ram_fail counted; the next attempt succeeds.
    bench_init(&b, 131);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_RESTORE, 1, 1, 0, 0, 0, 0, 1, BODY_V1);
    start_handoff(&b, 2);
    sim_fail(&b.w, 0, DEV_SPI, REC_LO, REC_HI, NOR_EIO);
    b.w.hook = corrupt_qspi_early;
    b.w.hook_arg = &b.q;
    b.w.hook_dev = DEV_QSPI;
    b.w.hook_addr = (IMG_HDR_SIZE + BODY_V1 - 1) & ~(NOR_PAGE - 1);
    handoff_load(&b.ho);
    install_t in2 = {.qspi = &b.q.nor, .spi = &b.s.nor, .ho = &b.ho};
    r = install_run(&in2);
    img_hdr_t h;
    CHECK(r == INSTALL_REBOOT && img_check(&b.q.nor, QSPI_SLOT, NULL, &h) == IMG_ESHA && b.ho.ram_fail == 1,
          "RAM copy done but the SHA is wrong: reboot, never a jump (r %d)", r);
    o = boot_until_stable(&b, APP_CONFIRM, 5);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.degraded && b.ho.ram_fail == 0, "then restored on the next attempt");
    bench_free(&b);
}

static void fail_record_reads(sim_world_t *w, void *arg)
{
    (void)arg;
    sim_fail(w, 0, DEV_SPI, REC_LO, REC_HI, SIM_READ_EIO);
}

static void test_trial_readback_error(void)
{
    // D7: the TRIAL record is fully written, but its read-back fails. The boot runs degraded (row 4)
    // and, the record being valid, the next boot sees the trial counted.
    bench_t b;
    bench_init(&b, 140);
    put_image(&b.q, QSPI_SLOT, 2, BODY_V2);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_TRIAL, 0, 0, 0, 0, 2, BODY_V2, 1, BODY_V1);
    bootrec_t r0;
    bootrec_read(&b.s.nor, &r0);
    uint32_t next = r0.seq == 1 ? SPI_REC_B : SPI_REC_A;  // the copy bootrec_write uses next
    b.w.hook = fail_record_reads;
    b.w.hook_dev = DEV_SPI;
    b.w.hook_addr = next + NOR_PAGE;  // before the CRC page is programmed
    handoff_load(&b.ho);
    install_t in = {.qspi = &b.q.nor, .spi = &b.s.nor, .ho = &b.ho};
    install_result_t r = install_run(&in);
    CHECK(r == INSTALL_JUMP && in.degraded && (in.rows & INSTALL_ROW(4)), "TRIAL record read-back fails: degraded run");
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    bootrec_t r1;
    CHECK(bootrec_read(&b.s.nor, &r1) == BOOTREC_VALID && r1.state == BR_TRIAL && r1.trial == 1,
          "the record was written all the same: the trial is counted (trial %u)", r1.trial);
    bench_free(&b);
}

static void test_restore_power_losses(void)
{
    // Power lost again and again during RESTORE (every 40 operations, 8 times): the attempts may run
    // out, the rescue retry starts over, and the board ends on the restored image.
    bench_t *b = malloc(sizeof *b);
    bench_init(b, 150);
    put_image(&b->s, SPI_BACKUP, 1, BODY_V1);
    record(b, BR_RESTORE, 0, 0, 0, 0, 0, 0, 1, BODY_V1);
    b->w.cut_at = b->w.ops + 40;
    b->w.cuts_left = 7;
    b->w.cut_period = 40;
    outcome_t o = boot_until_stable(b, APP_CONFIRM, 60);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.state == BR_CONFIRMED && b->cuts == 8,
          "8 power cuts during RESTORE: restored (kind %d, cuts %d)", o.kind, b->cuts);
    bench_free(b);
    free(b);
}

// ---- read errors are not proof of a bad image (Codex final review of Plan 04a) -------------------

static void test_image_read_errors(void)
{
    bench_t b;
    outcome_t o;
    // Only QSPI holds a valid image, and its header read fails once: decide again after a reset,
    // never halt.
    bench_init(&b, 160);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);
    sim_fail(&b.w, 0, DEV_QSPI, QSPI_SLOT, QSPI_SLOT + IMG_HDR_SIZE, SIM_READ_EIO);
    b.w.fail[0].count = 1;
    b.boots = 0;
    o = boot_until_stable(&b, APP_CONFIRM, 5);
    CHECK(o.kind == OUT_RUN && o.version == 1 && b.boots == 2, "QSPI read fails once: one more boot, then v1 runs (kind %d, boots %d)",
          o.kind, b.boots);
    bench_free(&b);

    // QSPI bad, the backup the only source, its read fails once: restored after a reset.
    bench_init(&b, 161);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_CONFIRMED, 0, 0, 0, 0, 0, 0, 1, BODY_V1);
    sim_fail(&b.w, 0, DEV_SPI, SPI_BACKUP, SPI_BACKUP + IMG_HDR_SIZE, SIM_READ_EIO);
    b.w.fail[0].count = 1;
    o = boot_until_stable(&b, APP_CONFIRM, 10);
    CHECK(o.kind == OUT_RUN && o.version == 1, "backup read fails once: restored (kind %d)", o.kind);
    bench_free(&b);

    // The backup stays unreadable: after 3 resets the boot gives up deciding again, but with an
    // unreadable source it retries every 60 s instead of halting; once it reads, it is restored.
    bench_init(&b, 162);
    put_image(&b.s, SPI_BACKUP, 1, BODY_V1);
    record(&b, BR_CONFIRMED, 0, 0, 0, 0, 0, 0, 1, BODY_V1);
    sim_fail(&b.w, 0, DEV_SPI, SPI_BACKUP, SPI_BACKUP + IMG_HDR_SIZE, SIM_READ_EIO);
    b.w.fail[0].count = 8;
    o = boot_until_stable(&b, APP_CONFIRM, 20);
    CHECK(o.kind == OUT_RUN && o.version == 1 && b.retries > 0, "backup unreadable for a while: rescue retries, then restored (kind %d)",
          o.kind);
    bench_free(&b);

    // Row 1 before everything: no readable record and no valid image at all halts at once.
    bench_init(&b, 163);
    sim_fail(&b.w, 0, DEV_SPI, REC_LO, REC_HI, SIM_READ_EIO);
    o = boot_until_stable(&b, APP_CONFIRM, 5);
    CHECK(o.kind == OUT_HALT && b.boots == 1, "unreadable record, nothing valid: halted on the first boot (boots %d)", b.boots);
    bench_free(&b);
}

// ---- the staging writer -------------------------------------------------------------------------

static int upload_raw(bench_t *b, const uint8_t *img, uint32_t total, uint32_t send)
{
    ota_stage_t st;
    handoff_t ho;
    memset(&ho, 0, sizeof ho);
    int r = ota_stage_begin(&st, &b->s.nor, &ho, total);
    for (uint32_t i = 0; !r && i < send; i += 777)
        r = ota_stage_write(&st, img + i, send - i < 777 ? send - i : 777);
    if (!r)
        r = ota_stage_finish(&st);
    return r;
}

static void test_upload_errors(void)
{
    bench_t b;
    bench_init(&b, 90);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(&b, APP_CONFIRM, 3);
    uint32_t len;
    uint8_t *img = make_image(2, BODY_V2, &len);
    img[IMG_HDR_SIZE + 5000] ^= 1;  // body no longer matches the header's SHA-256
    CHECK(upload_raw(&b, img, len, len) == OTA_EIMAGE, "upload with a wrong SHA-256: 400");
    img[IMG_HDR_SIZE + 5000] ^= 1;
    img[6] ^= 1;  // hardware id (the header CRC fails first, which is the same answer)
    CHECK(upload_raw(&b, img, len, len) == OTA_EIMAGE, "upload for another board: 400");
    img[6] ^= 1;
    CHECK(upload_raw(&b, img, len, len - 100) == OTA_ESHORT, "upload that ends early: 400");
    CHECK(upload_raw(&b, img, len - 4, len - 4) == OTA_EIMAGE, "announced length differs from the header: 400");
    CHECK(upload_raw(&b, img, SLOT_SIZE + 1, 0) == OTA_ETOOBIG, "upload larger than the slot: 413");
    bootrec_t r;
    CHECK(bootrec_read(&b.s.nor, &r) == BOOTREC_VALID && r.state == BR_CONFIRMED, "failed uploads leave the boot state alone");
    free(img);
    bench_free(&b);
}

// ---- power cuts at every operation --------------------------------------------------------------

typedef struct {
    const char *name;
    int policy;
    uint16_t want;        // the image expected at the end when the cut came after PENDING
} matrix_t;

// One run: v1 confirmed, the app uploads v2 (PENDING), boots until stable. cut < 0: no cut.
// Returns the number of operations of the whole run and fills o / pending_done.
// The bench lives on the heap: it changes between setjmp and longjmp, and a local object would be
// indeterminate afterwards (C11 7.13.2.1p3). cuts > 1 adds cuts every `period` operations.
static long one_run(const matrix_t *m, long cut, int cuts, long period, outcome_t *o, long *pending_done)
{
    bench_t *b = malloc(sizeof *b);
    bench_init(b, 1000 + (uint32_t)cut * 7 + (uint32_t)period);
    put_image(&b->q, QSPI_SLOT, 1, BODY_V1);
    boot_until_stable(b, APP_CONFIRM, 3);
    b->w.ops = 0;
    b->w.cut_at = cut;
    b->w.cuts_left = cuts - 1;
    b->w.cut_period = period;
    if (!setjmp(b->jb)) {
        upload(b, 2, BODY_V2);
        if (pending_done)
            *pending_done = b->w.ops;
    } else {
        b->cuts++;  // cut during the upload
        memset(&b->ho, 0xA5, sizeof b->ho);
    }
    b->boots = 0;
    *o = boot_until_stable(b, m->policy, 60);
    o->cuts = b->cuts;
    long ops = b->w.ops;
    bench_free(b);
    free(b);
    return ops;
}

static void test_power_cuts(const matrix_t *m)
{
    outcome_t o;
    long pending = 0;
    long total = one_run(m, -1, 1, 0, &o, &pending);
    CHECK(o.kind == OUT_RUN && o.version == m->want && o.state == BR_CONFIRMED, "%s without cuts", m->name);
    int bad = 0, newer = 0;
    for (long cut = 0; cut < total; cut++) {
        one_run(m, cut, 1, 0, &o, NULL);
        bool ok = o.kind == OUT_RUN && o.state == BR_CONFIRMED && !o.degraded && (o.version == 1 || o.version == 2);
        if (cut >= pending)
            ok = ok && o.version == m->want;  // one cut after PENDING costs an attempt, never the install
        if (!ok && bad++ < 5)
            printf("  %s: cut at op %ld/%ld -> kind %d v%u state %u degraded %d\n", m->name, cut, total, o.kind,
                   o.version, o.state, o.degraded);
        newer += o.version == 2;
    }
    CHECK(bad == 0, "%s: %d of %ld cut points end wrong", m->name, bad, total);
    printf("%s: %ld cut points (PENDING done at op %ld), %d ended on v2\n", m->name, total, pending, newer);

    // Several cuts in one run: up to three cuts from a random start, a random 1-200 operations apart
    // (a cut that ends the upload before PENDING leaves nothing more to cut). With repeated cuts both
    // images are acceptable; never a halt, a degraded run or an unconfirmed end.
    uint32_t x = 12345;
    int triple = 0;
    bad = 0;
    for (int i = 0; i < 300; i++) {
        x = x * 1103515245u + 12345u;
        long cut = (long)((x >> 8) % (uint32_t)total), period = 1 + (long)((x >> 4) % 200);
        one_run(m, cut, 3, period, &o, NULL);
        triple += o.cuts == 3;
        bool ok = o.kind == OUT_RUN && o.state == BR_CONFIRMED && !o.degraded && (o.version == 1 || o.version == 2);
        if (!ok && bad++ < 5)
            printf("  %s: 3 cuts from op %ld every %ld -> kind %d v%u state %u\n", m->name, cut, period, o.kind,
                   o.version, o.state);
    }
    CHECK(bad == 0, "%s: %d of 300 multi-cut runs end wrong", m->name, bad);
    CHECK(triple >= 100, "%s: only %d of 300 runs had all three cuts", m->name, triple);
    printf("%s: 300 runs with up to three cuts, %d with all three\n", m->name, triple);
}

int main(void)
{
    test_handoff();
    test_first_boot_and_blank();
    test_records();
    test_image_sizes();
    test_staging_bad();
    test_trial();
    test_verify_and_restore_checks();
    test_exhausted();
    test_deadlines();
    test_record_failures();
    test_silent_damage();
    test_upload_errors();
    test_read_faults();
    test_record_read_errors();
    test_rescue_retry_keeps_qspi();
    test_ram_path_bad_result();
    test_trial_readback_error();
    test_restore_power_losses();
    test_image_read_errors();
    matrix_t install = {"install", APP_CONFIRM, 2}, rollback = {"rollback", APP_CRASH, 1};
    if (!getenv("OTA_QUICK")) {  // the two matrices take most of the run time
        test_power_cuts(&install);
        test_power_cuts(&rollback);
    }
    for (int row = 1; row <= 10; row++)
        CHECK(g_rows & INSTALL_ROW(row), "decision-table row %d was never exercised", row);
    printf("test_install: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
