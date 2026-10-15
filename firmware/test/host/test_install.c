// Host tests for the bootloader's install logic and the staging writer (spec §11.2, §11.3, §12.1):
// make -C stm32/firmware/test/host
// Simulated QSPI and SPI4 (nor_sim): power cuts at every erase and program, deadlines, flash
// errors, silent bad programming, read errors. Invariant checked on every boot: the bootloader
// never jumps into an image that fails img_check.
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
#define BODY_V1 70000u  // 18 sectors with the header
#define BODY_V2 90000u  // 23 sectors

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

static bool blank(nor_sim_t *n, uint32_t off, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        if (n->mem[off + i] != 0xFF)
            return false;
    return true;
}

// ---- the bench ----------------------------------------------------------------------------

typedef struct {
    sim_world_t w;
    nor_sim_t q, s;
    jmp_buf jb;
    int boots, cuts;
    uint8_t *upload_img;  // owned here: a power cut can longjmp out of upload()
} bench_t;

static void bench_init(bench_t *b, uint32_t seed)
{
    memset(b, 0, sizeof *b);
    sim_world_init(&b->w, seed);
    b->w.cut_jmp = &b->jb;
    nor_sim_init(&b->q, &b->w, DEV_QSPI, 0x400000);
    nor_sim_init(&b->s, &b->w, DEV_SPI, 0x400000);
}

static void bench_free(bench_t *b)
{
    free(b->upload_img);
    nor_sim_free(&b->q);
    nor_sim_free(&b->s);
}

// A board as it leaves production and has booted once: v1 in QSPI and in the factory slot.
static void bench_v1(bench_t *b, uint32_t seed)
{
    bench_init(b, seed);
    put_image(&b->q, QSPI_SLOT, 1, BODY_V1);
    put_image(&b->s, SPI_FACTORY, 1, BODY_V1);
}

static int upload_raw(bench_t *b, const uint8_t *img, uint32_t total, uint32_t send)
{
    ota_stage_t st;
    int r = ota_stage_begin(&st, &b->s.nor, total);
    for (uint32_t i = 0; !r && i < send; i += 777)
        r = ota_stage_write(&st, img + i, send - i < 777 ? send - i : 777);
    if (!r)
        r = ota_stage_finish(&st);
    return r;
}

static int upload(bench_t *b, uint16_t ver, uint32_t body)
{
    uint32_t len;
    free(b->upload_img);
    b->upload_img = make_image(ver, body, &len);
    return upload_raw(b, b->upload_img, len, len);
}

enum { OUT_RUN = 1, OUT_HALT, OUT_CUT };

typedef struct {
    int kind;
    uint16_t version;  // OUT_RUN: the version that runs
    unsigned did;
    install_why_t why;
    long ops;  // erases and programs during this boot
} outcome_t;

// One boot. A power cut (longjmp out of an erase or program) ends it with OUT_CUT. On INSTALL_JUMP
// the image in QSPI is checked again here: the bootloader must never jump into one that fails.
static outcome_t boot(bench_t *b, bool reload)
{
    outcome_t o = {0};
    long ops0 = b->w.ops;
    b->boots++;
    if (setjmp(b->jb)) {  // nothing of this frame is read after the jump but o, which it never changed
        b->cuts++;
        o.kind = OUT_CUT;
        return o;
    }
    install_t in = {.qspi = &b->q.nor, .spi = &b->s.nor, .reload = reload};
    install_result_t r = install_run(&in);
    o.did = in.did;
    o.why = in.why;
    o.ops = b->w.ops - ops0;
    if (r == INSTALL_HALT) {
        CHECK(in.why != WHY_NONE, "a halt without a reason");
        o.kind = OUT_HALT;
        return o;
    }
    CHECK(r == INSTALL_JUMP && in.why == WHY_NONE, "jump with result %d, reason %d", r, in.why);
    img_hdr_t h;
    int ok = img_check(&b->q.nor, QSPI_SLOT, NULL, &h);
    CHECK(ok == IMG_OK, "jumped into an image that fails its check (%d)", ok);
    o.kind = ok == IMG_OK ? OUT_RUN : OUT_HALT;
    o.version = ok == IMG_OK ? h.version[0] : 0;
    return o;
}

// Boots until the app runs or the bootloader halts; power cuts just boot again.
static outcome_t boot_until_settled(bench_t *b, bool reload, int max_boots)
{
    outcome_t o = {0};
    for (int i = 0; i < max_boots; i++) {
        o = boot(b, reload);
        if (o.kind != OUT_CUT)
            return o;
    }
    return o;
}

// ---- plain cases ----------------------------------------------------------------------------

static void test_first_boot(void)
{
    bench_t b;
    bench_init(&b, 1);
    outcome_t o = boot(&b, false);
    CHECK(o.kind == OUT_HALT && o.why == WHY_NO_IMAGE && o.ops == 0, "blank board: halt, nothing written");
    CHECK(blank(&b.s, SPI_FACTORY, NOR_SECTOR), "blank board: the factory slot is not seeded from nothing");
    bench_free(&b);

    bench_init(&b, 2);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.did == DID_SEED, "first boot after production: seeds the factory slot (did %u)", o.did);
    CHECK(version_at(&b.s, SPI_FACTORY) == 1, "the factory slot holds v1");
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.did == 0 && o.ops == 0, "second boot: nothing written");
    bench_free(&b);

    bench_init(&b, 3);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    damage(&b.q, QSPI_SLOT);
    o = boot(&b, false);
    CHECK(o.kind == OUT_HALT && o.why == WHY_NO_IMAGE && o.ops == 0, "bad QSPI, blank factory: halt, nothing written");
    bench_free(&b);
}

static void test_install(void)
{
    bench_t b;
    bench_v1(&b, 10);
    CHECK(upload(&b, 2, BODY_V2) == OTA_OK, "upload v2");
    CHECK(version_at(&b.q, QSPI_SLOT) == 1, "the upload does not touch QSPI");
    outcome_t o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 2 && o.did == DID_INSTALL, "the next boot installs v2 (did %u)", o.did);
    CHECK(version_at(&b.s, SPI_FACTORY) == 1, "the factory slot keeps v1");
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 2 && o.ops == 0, "and the boot after writes nothing");
    CHECK(upload(&b, 2, BODY_V2) == OTA_OK && boot(&b, false).ops == 0, "uploading the running image again: nothing installed");
    CHECK(upload(&b, 1, BODY_V1) == OTA_OK, "upload of an older image");
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.did == DID_INSTALL, "an older image is installed too (the app accepted it)");
    bench_free(&b);

    uint32_t sizes[] = {8, IMG_MAX_BODY};  // the shortest body and the longest
    for (int i = 0; i < 2; i++) {
        bench_v1(&b, 11 + i);
        CHECK(upload(&b, 3, sizes[i]) == OTA_OK, "upload a %u-byte body", (unsigned)sizes[i]);
        o = boot(&b, false);
        CHECK(o.kind == OUT_RUN && o.version == 3 && o.did == DID_INSTALL, "a %u-byte body installs", (unsigned)sizes[i]);
        bench_free(&b);
    }
}

static void test_staging_not_installed(void)
{
    bench_t b;
    bench_v1(&b, 20);
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    damage(&b.s, SPI_STAGING);
    outcome_t o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.ops == 0, "staging with a bad SHA: v1 runs, nothing written");
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    b.s.mem[SPI_STAGING + 40] ^= 1;  // inside the CRC-covered header core
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.ops == 0, "staging with a bad header: v1 runs, nothing written");
    bench_free(&b);

    bench_v1(&b, 21);
    put_image(&b.s, SPI_STAGING, 1, BODY_V1);
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.ops == 0, "staging holds the running image: nothing written");
    bench_free(&b);
}

static void test_qspi_bad(void)
{
    bench_t b;
    bench_v1(&b, 30);
    damage(&b.q, QSPI_SLOT);
    outcome_t o = boot(&b, false);
    CHECK(o.kind == OUT_HALT && o.why == WHY_NO_IMAGE && o.ops == 0, "bad QSPI, no staging: halt, the factory image is not used by itself");
    o = boot(&b, true);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.did == DID_RESTORE, "then Reload restores the factory image");
    bench_free(&b);

    bench_v1(&b, 31);
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    damage(&b.q, QSPI_SLOT);
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 2 && o.did == DID_INSTALL, "bad QSPI, valid staging: installs it");
    bench_free(&b);

    bench_v1(&b, 32);
    put_image(&b.s, SPI_STAGING, 1, BODY_V1);
    damage(&b.q, QSPI_SLOT);  // same header as staging, broken body
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.did == DID_INSTALL, "QSPI with staging's header but a broken body: installed again");
    bench_free(&b);
}

static void test_restore(void)
{
    bench_t b;
    bench_v1(&b, 40);
    CHECK(upload(&b, 2, BODY_V2) == OTA_OK && boot(&b, false).version == 2, "v2 installed");
    outcome_t o = boot(&b, true);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.did == DID_RESTORE, "Reload: the factory image runs (did %u)", o.did);
    CHECK(version_at(&b.s, SPI_STAGING) == 0, "and staging no longer holds an image");
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.ops == 0, "the boot after does not install v2 again");
    o = boot(&b, true);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.did == DID_RESTORE, "Reload again: restored again");
    bench_free(&b);

    bench_v1(&b, 41);
    CHECK(upload(&b, 2, BODY_V2) == OTA_OK && boot(&b, false).version == 2, "v2 installed");
    damage(&b.s, SPI_FACTORY);
    o = boot(&b, true);
    CHECK(o.kind == OUT_HALT && o.why == WHY_FACTORY_BAD && o.ops == 0, "Reload with a bad factory image: halt, nothing written");
    CHECK(version_at(&b.q, QSPI_SLOT) == 2 && version_at(&b.s, SPI_STAGING) == 2, "QSPI and staging untouched");
    bench_free(&b);

    bench_v1(&b, 42);
    CHECK(upload(&b, 2, BODY_V2) == OTA_OK && boot(&b, false).version == 2, "v2 installed");
    sim_fail(&b.w, 0, DEV_SPI, SPI_STAGING, SPI_STAGING + NOR_SECTOR, NOR_EIO);
    o = boot(&b, true);
    CHECK(o.kind == OUT_HALT && o.why == WHY_STAGING, "Reload, staging cannot be erased: halt");
    CHECK(version_at(&b.q, QSPI_SLOT) == 2, "before QSPI is touched");
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    bench_free(&b);
}

static void test_copy_failures(void)
{
    int codes[] = {NOR_EIO, NOR_TIMEOUT, SIM_SILENT};
    const char *names[] = {"flash error", "deadline", "silent bad program"};
    for (int i = 0; i < 3; i++) {
        bench_t b;
        bench_v1(&b, 50 + i);
        CHECK(upload(&b, 2, BODY_V2) == OTA_OK, "upload v2");
        sim_fail(&b.w, 0, DEV_QSPI, 0x8000, 0x9000, codes[i]);
        outcome_t o = boot(&b, false);
        CHECK(o.kind == OUT_HALT && o.why == WHY_COPY, "install, %s in QSPI: halt (kind %d why %d)", names[i], o.kind, o.why);
        o = boot(&b, false);
        CHECK(o.kind == OUT_HALT && o.why == WHY_COPY, "%s still there: halt again", names[i]);
        sim_fail(&b.w, 0, 0, 0, 0, 0);
        o = boot(&b, false);
        CHECK(o.kind == OUT_RUN && o.version == 2 && o.did == DID_INSTALL, "%s gone: the next boot installs v2", names[i]);

        sim_fail(&b.w, 0, DEV_QSPI, 0x8000, 0x9000, codes[i]);
        o = boot(&b, true);
        CHECK(o.kind == OUT_HALT && o.why == WHY_COPY, "restore, %s in QSPI: halt", names[i]);
        sim_fail(&b.w, 0, 0, 0, 0, 0);
        o = boot(&b, true);
        CHECK(o.kind == OUT_RUN && o.version == 1 && o.did == DID_RESTORE, "%s gone: Reload restores", names[i]);
        bench_free(&b);
    }
}

static void staging_reads_fail(sim_world_t *w, void *arg)
{
    sim_fail(w, 1, DEV_SPI, SPI_STAGING, SPI_STAGING + SLOT_SIZE, SIM_READ_EIO);
}

static void test_read_errors(void)
{
    bench_t b;
    bench_v1(&b, 60);
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    sim_fail(&b.w, 0, DEV_SPI, SPI_STAGING, SPI_STAGING + SLOT_SIZE, SIM_READ_EIO);
    outcome_t o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.ops == 0, "staging unreadable: v1 runs, nothing written");
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    bench_free(&b);

    bench_v1(&b, 61);
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    b.w.hook_dev = DEV_QSPI;  // after staging passed its check: its reads fail during the copy
    b.w.hook_addr = 0;
    b.w.hook = staging_reads_fail;
    o = boot(&b, false);
    CHECK(o.kind == OUT_HALT && o.why == WHY_COPY, "staging unreadable during the copy: halt");
    sim_fail(&b.w, 1, 0, 0, 0, 0);
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 2 && o.did == DID_INSTALL, "readable again: installed");
    bench_free(&b);

    bench_v1(&b, 62);
    sim_fail(&b.w, 0, DEV_QSPI, 0, SLOT_SIZE, SIM_READ_EIO);
    o = boot(&b, false);
    CHECK(o.kind == OUT_HALT && o.why == WHY_NO_IMAGE && o.ops == 0, "QSPI unreadable, no staging: halt, nothing written");
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    bench_free(&b);

    bench_init(&b, 63);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    sim_fail(&b.w, 0, DEV_SPI, SPI_FACTORY, SPI_FACTORY + SLOT_SIZE, SIM_READ_EIO);
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.ops == 0, "factory slot unreadable: not taken for blank, not seeded");
    o = boot(&b, true);
    CHECK(o.kind == OUT_HALT && o.why == WHY_FACTORY_BAD && o.ops == 0, "and Reload halts without writing");
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    bench_free(&b);
}

static void test_seed(void)
{
    bench_t b;
    bench_v1(&b, 70);
    damage(&b.s, SPI_FACTORY);
    outcome_t o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.ops == 0, "a factory slot with a header but a bad image is never seeded again");
    bench_free(&b);

    bench_init(&b, 71);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    sim_fail(&b.w, 0, DEV_SPI, SPI_FACTORY + 0x8000, SPI_FACTORY + 0x9000, NOR_EIO);
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.did == DID_SEED_FAIL, "seeding fails: the app still runs (did %u)", o.did);
    CHECK(blank(&b.s, SPI_FACTORY, 4), "and the factory slot still has no magic");
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.did == DID_SEED && version_at(&b.s, SPI_FACTORY) == 1, "the next boot seeds it");
    bench_free(&b);

    bench_init(&b, 72);
    put_image(&b.q, QSPI_SLOT, 1, BODY_V1);
    put_image(&b.s, SPI_STAGING, 2, BODY_V2);
    o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 2 && o.did == (DID_SEED | DID_INSTALL), "seeding comes before an install (did %u)", o.did);
    CHECK(version_at(&b.s, SPI_FACTORY) == 1, "so the factory image is the production image");
    bench_free(&b);
}

static void test_upload_errors(void)
{
    bench_t b;
    bench_v1(&b, 90);
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
    outcome_t o = boot(&b, false);
    CHECK(o.kind == OUT_RUN && o.version == 1 && o.ops == 0, "after failed uploads v1 runs and nothing is installed");
    sim_fail(&b.w, 0, DEV_SPI, SPI_STAGING + 0x4000, SPI_STAGING + 0x5000, SIM_SILENT);
    CHECK(upload_raw(&b, img, len, len) == OTA_EFLASH, "staging programmed wrong: the read-back check answers 500");
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    free(img);
    bench_free(&b);
}

// ---- power cuts at every operation --------------------------------------------------------------

// The three ways the bootloader writes, each run once without cuts to count its operations, then
// again with a power cut (or several) at every operation. The bench lives on the heap: it changes
// between setjmp and longjmp, and a local object would be indeterminate afterwards (C11 7.13.2.1p3).
enum { RUN_INSTALL, RUN_RESTORE, RUN_SEED };
static const char *run_names[] = {"install", "restore", "seed"};

typedef struct {
    outcome_t end;  // after the cut(s): the boots until settled, without Reload
    outcome_t again;  // RUN_RESTORE: one more boot, with Reload
    int cuts;
    bool reinstalled;  // RUN_RESTORE: some boot installed staging after the restore began
    uint16_t factory;  // RUN_SEED: the factory slot's version at the end
} result_t;

static void arm(bench_t *b, long cut, int cuts, long period)
{
    b->w.ops = 0;
    b->w.cut_at = cut;
    b->w.cuts_left = cuts - 1;
    b->w.cut_period = period;
}

// Returns the operations of the run; *upload_end: the operation count when the upload finished.
static long one_run(int kind, long cut, int cuts, long period, result_t *res, long *upload_end)
{
    bench_t *b = malloc(sizeof *b);
    memset(res, 0, sizeof *res);
    uint32_t seed = 1000 + (uint32_t)cut * 7 + (uint32_t)period;
    if (kind == RUN_SEED) {
        bench_init(b, seed);
        put_image(&b->q, QSPI_SLOT, 1, BODY_V1);
    } else {
        bench_v1(b, seed);
    }
    if (kind == RUN_RESTORE) {
        upload(b, 2, BODY_V2);
        boot(b, false);  // v2 installed, staging still holds v2
    }
    arm(b, cut, cuts, period);
    if (kind == RUN_INSTALL) {
        if (!setjmp(b->jb)) {
            upload(b, 2, BODY_V2);
            if (upload_end)
                *upload_end = b->w.ops;
        } else {
            b->cuts++;  // cut during the upload
        }
    }
    for (int i = 0; i < 10; i++) {  // the boots with cuts (RUN_RESTORE: Reload held), until the cuts stop
        outcome_t o = boot(b, kind == RUN_RESTORE);
        res->reinstalled |= kind == RUN_RESTORE && (o.did & DID_INSTALL);
        if (o.kind != OUT_CUT || b->w.cut_at < 0 || b->cuts >= cuts)
            break;
    }
    long ops = b->w.ops;
    b->w.cut_at = -1;
    res->end = boot_until_settled(b, false, 3);
    res->reinstalled |= kind == RUN_RESTORE && (res->end.did & DID_INSTALL);
    if (kind == RUN_RESTORE)
        res->again = boot(b, true);
    res->factory = version_at(&b->s, SPI_FACTORY);
    res->cuts = b->cuts;
    bench_free(b);
    free(b);
    return ops;
}

// What every run must end with. single: exactly one cut, at operation cut.
static bool good(int kind, const result_t *r, long cut, long upload_end, bool single)
{
    switch (kind) {
    case RUN_INSTALL:  // cut before the upload finished: v1; after it: v2. Several cuts: either.
        if (r->end.kind != OUT_RUN)
            return false;
        if (single)
            return r->end.version == (cut < upload_end ? 1 : 2);
        return r->end.version == 1 || r->end.version == 2;
    case RUN_RESTORE:  // never staging again; v2 untouched, v1, or a halt; Reload then gives v1
        return !r->reinstalled && (r->end.kind == OUT_HALT || (r->end.kind == OUT_RUN && r->end.version >= 1)) &&
               r->again.kind == OUT_RUN && r->again.version == 1;
    default:  // RUN_SEED: the app runs, and the factory slot ends seeded
        return r->end.kind == OUT_RUN && r->end.version == 1 && r->factory == 1;
    }
}

static void test_power_cuts(int kind)
{
    const char *name = run_names[kind];
    result_t r;
    long upload_end = 0;
    long total = one_run(kind, -1, 1, 0, &r, &upload_end);
    CHECK(good(kind, &r, total, upload_end, true), "%s without cuts", name);
    int bad = 0;
    for (long cut = 0; cut < total; cut++) {
        one_run(kind, cut, 1, 0, &r, NULL);
        bool ok = r.cuts == 1 && good(kind, &r, cut, upload_end, true);
        if (!ok && bad++ < 5)
            printf("  %s: cut at op %ld/%ld -> kind %d v%u why %d factory v%u reinstalled %d again %d/v%u\n", name, cut,
                   total, r.end.kind, r.end.version, r.end.why, r.factory, r.reinstalled, r.again.kind, r.again.version);
    }
    CHECK(bad == 0, "%s: %d of %ld cut points end wrong", name, bad, total);

    // Up to three cuts from a random start, a random 1-200 operations apart.
    uint32_t x = 12345 + (uint32_t)kind;
    int triple = 0;
    bad = 0;
    for (int i = 0; i < 300; i++) {
        x = x * 1103515245u + 12345u;
        long cut = (long)((x >> 8) % (uint32_t)total), period = 1 + (long)((x >> 4) % 200);
        one_run(kind, cut, 3, period, &r, NULL);
        triple += r.cuts == 3;
        if (!good(kind, &r, cut, upload_end, false) && bad++ < 5)
            printf("  %s: 3 cuts from op %ld every %ld -> kind %d v%u why %d\n", name, cut, period, r.end.kind,
                   r.end.version, r.end.why);
    }
    CHECK(bad == 0, "%s: %d of 300 multi-cut runs end wrong", name, bad);
    CHECK(triple >= 100, "%s: only %d of 300 runs had all three cuts", name, triple);
    printf("%s: %ld cut points, 300 runs with up to three cuts (%d with all three)\n", name, total, triple);
}

// A few cut points of each matrix, in the quick run too: the first operations, the middle, and the
// last three (the header page and the magic of the last copy).
static void test_spot_cuts(void)
{
    for (int kind = RUN_INSTALL; kind <= RUN_SEED; kind++) {
        result_t r;
        long upload_end = 0;
        long total = one_run(kind, -1, 1, 0, &r, &upload_end);
        long cuts[] = {0, 1, 5, upload_end, total / 2, total - 3, total - 2, total - 1};
        for (int i = 0; i < 8; i++) {
            one_run(kind, cuts[i], 1, 0, &r, NULL);
            CHECK(r.cuts == 1 && good(kind, &r, cuts[i], upload_end, true), "%s: cut at op %ld of %ld ends wrong (kind %d v%u)",
                  run_names[kind], cuts[i], total, r.end.kind, r.end.version);
        }
    }
}

int main(void)
{
    test_first_boot();
    test_install();
    test_staging_not_installed();
    test_qspi_bad();
    test_restore();
    test_copy_failures();
    test_read_errors();
    test_seed();
    test_upload_errors();
    test_spot_cuts();
    if (!getenv("OTA_QUICK"))  // the matrices take most of the run time
        for (int k = RUN_INSTALL; k <= RUN_SEED; k++)
            test_power_cuts(k);
    printf("test_install: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
