// Host tests for the anti-clone binding (spec §11.5, §12.1): make -C stm32/firmware/test/host
// Simulated QSPI (nor_sim): first-boot write, power cuts at every operation of that write, a record
// from another chip or key, a corrupt record, read and write errors, silent bad programming.
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bind.h"
#include "flashmap.h"
#include "nor_sim.h"

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

static uint8_t KEY[BIND_KEY_LEN], KEY2[BIND_KEY_LEN], UID_A[BIND_UID_LEN], UID_B[BIND_UID_LEN];

static void init_ids(void)
{
    for (int i = 0; i < 32; i++) {
        KEY[i] = (uint8_t)i;
        KEY2[i] = (uint8_t)(i + 1);
    }
    for (int i = 0; i < 12; i++) {
        UID_A[i] = (uint8_t)(0x10 + i);
        UID_B[i] = (uint8_t)(0x10 + i);
    }
    UID_B[11] ^= 1;  // the next chip off the line: one bit apart
}

typedef struct {
    sim_world_t w;
    nor_sim_t q;
    jmp_buf jb;
} bench_t;

// A board with an application in the slot (a pattern that is never 0xFF) and no record.
static void bench_init(bench_t *b, uint32_t seed)
{
    memset(b, 0, sizeof *b);
    sim_world_init(&b->w, seed);
    b->w.cut_jmp = &b->jb;
    nor_sim_init(&b->q, &b->w, DEV_QSPI, 0x400000);
    for (uint32_t i = 0; i < SLOT_SIZE; i++)
        b->q.mem[QSPI_SLOT + i] = (uint8_t)(i * 7 + 1) & 0x7F;
}

static bool slot_intact(bench_t *b)
{
    for (uint32_t i = 0; i < SLOT_SIZE; i++)
        if (b->q.mem[QSPI_SLOT + i] != ((uint8_t)(i * 7 + 1) & 0x7F))
            return false;
    return true;
}

static bool slot_erased(bench_t *b)
{
    for (uint32_t i = 0; i < SLOT_SIZE; i++)
        if (b->q.mem[QSPI_SLOT + i] != 0xFF)
            return false;
    return true;
}

static bool has_record(bench_t *b, const uint8_t key[32], const uint8_t uid[12])
{
    return bind_matches(b->q.mem + QSPI_BIND, key, uid);
}

static int check(bench_t *b, const uint8_t key[32], const uint8_t uid[12], long *ops)
{
    long o = b->w.ops;
    int r = bind_check(&b->q.nor, key, uid, NULL);
    if (ops)
        *ops = b->w.ops - o;
    return r;
}

static void test_record(void)
{
    // The same record computed with Python's hmac/zlib (key 00..1f, UID 10..1b).
    static const uint8_t want[BIND_LEN] = {
        0x53, 0x42, 0x42, 0x44, 0x58, 0x51, 0xcc, 0xf0, 0xe1, 0x17, 0xc6, 0x49, 0xfb, 0x9f, 0xb5, 0x07, 0x1b, 0x4d, 0x01, 0x08,
        0x94, 0xd6, 0xeb, 0x17, 0x1a, 0xc5, 0x6a, 0x13, 0xa5, 0x9c, 0xd1, 0x48, 0xf8, 0x85, 0x53, 0x81, 0x89, 0x72, 0xd8, 0x34};
    uint8_t rec[BIND_LEN];
    bind_record(KEY, UID_A, rec);
    CHECK(!memcmp(rec, want, BIND_LEN), "bind_record matches the Python reference");
    CHECK(bind_matches(want, KEY, UID_A), "the app accepts its own chip's record");
    CHECK(!bind_matches(want, KEY, UID_B), "and refuses it on another chip");
    CHECK(!bind_matches(want, KEY2, UID_A), "and with another key");
    uint8_t blank[BIND_LEN];
    memset(blank, 0xFF, sizeof blank);
    CHECK(!bind_matches(blank, KEY, UID_A), "and refuses a missing record");
}

static void test_first_boot(void)
{
    bench_t b;
    bench_init(&b, 1);
    long ops;
    CHECK(check(&b, KEY, UID_A, &ops) == BIND_WRITTEN && has_record(&b, KEY, UID_A), "first boot: the record is written");
    CHECK(check(&b, KEY, UID_A, &ops) == BIND_OK && ops == 0, "next boot: it matches, nothing written");
    CHECK(slot_intact(&b), "the application is untouched");
    for (uint32_t i = BIND_LEN; i < NOR_SECTOR; i++)
        if (b.q.mem[QSPI_BIND + i] != 0xFF) {
            CHECK(0, "the rest of the record sector stays 0xFF (byte %u)", (unsigned)i);
            break;
        }
    nor_sim_free(&b.q);
}

static void test_clone(void)
{
    const uint8_t *keys[2] = {KEY, KEY2}, *uids[2] = {UID_B, UID_A};
    const char *what[2] = {"another chip", "another key"};
    for (int i = 0; i < 2; i++) {
        bench_t b;
        bench_init(&b, 10 + i);
        CHECK(check(&b, KEY, UID_A, NULL) == BIND_WRITTEN, "board A bound");
        // QSPI copied whole onto another board (or a bootloader with another key)
        CHECK(check(&b, keys[i], uids[i], NULL) == BIND_CLONE, "%s: clone detected", what[i]);
        CHECK(slot_erased(&b), "%s: the application slot is erased", what[i]);
        CHECK(has_record(&b, KEY, UID_A), "%s: the original record is kept", what[i]);
        CHECK(check(&b, keys[i], uids[i], NULL) == BIND_CLONE, "%s: and the next boot still refuses", what[i]);
        nor_sim_free(&b.q);
    }
}

static void test_corrupt_and_errors(void)
{
    bench_t b;
    long ops;
    bench_init(&b, 20);
    CHECK(check(&b, KEY, UID_A, NULL) == BIND_WRITTEN, "bound");
    b.q.mem[QSPI_BIND + 10] ^= 0x01;  // a tag bit: the CRC no longer matches
    CHECK(check(&b, KEY, UID_A, &ops) == BIND_ECORRUPT && ops == 0, "corrupt record: halt, nothing written");
    CHECK(slot_intact(&b), "and the application is not erased");
    nor_sim_free(&b.q);

    bench_init(&b, 21);
    CHECK(check(&b, KEY, UID_A, NULL) == BIND_WRITTEN, "bound");
    sim_fail(&b.w, 0, DEV_QSPI, QSPI_BIND, QSPI_BIND + NOR_SECTOR, SIM_READ_EIO);
    CHECK(check(&b, KEY, UID_A, &ops) == BIND_EREAD && ops == 0, "record unreadable: halt, nothing written");
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    CHECK(check(&b, KEY, UID_A, &ops) == BIND_OK && ops == 0, "readable again: it matches");
    nor_sim_free(&b.q);

    bench_init(&b, 22);
    sim_fail(&b.w, 0, DEV_QSPI, QSPI_BIND, QSPI_BIND + NOR_SECTOR, SIM_READ_EIO);
    CHECK(check(&b, KEY, UID_A, &ops) == BIND_EREAD && ops == 0, "first boot, record unreadable: never taken for missing");
    nor_sim_free(&b.q);

    int codes[] = {NOR_EIO, NOR_TIMEOUT};
    for (int i = 0; i < 2; i++) {
        bench_init(&b, 23 + i);
        sim_fail(&b.w, 0, DEV_QSPI, QSPI_BIND, QSPI_BIND + NOR_SECTOR, codes[i]);
        CHECK(check(&b, KEY, UID_A, NULL) == BIND_EWRITE, "record write fails (%d): halt", codes[i]);
        sim_fail(&b.w, 0, 0, 0, 0, 0);
        CHECK(check(&b, KEY, UID_A, NULL) == BIND_WRITTEN && has_record(&b, KEY, UID_A), "the next boot writes it");
        nor_sim_free(&b.q);
    }

    // The tag programmed wrong without an error (the magic itself is fine): the read-back before
    // the magic catches it, so the record never gets a magic and the next boot writes it again.
    bench_init(&b, 25);
    sim_fail(&b.w, 0, DEV_QSPI, QSPI_BIND + 4, QSPI_BIND + BIND_LEN, SIM_SILENT);
    CHECK(check(&b, KEY, UID_A, NULL) == BIND_EWRITE, "tag programmed wrong silently: halt");
    CHECK(b.q.mem[QSPI_BIND] == 0xFF, "and the record has no magic");
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    CHECK(check(&b, KEY, UID_A, NULL) == BIND_WRITTEN && has_record(&b, KEY, UID_A), "the fault gone: written");
    nor_sim_free(&b.q);

    bench_init(&b, 26);
    sim_fail(&b.w, 0, DEV_QSPI, QSPI_BIND, QSPI_BIND + 4, SIM_SILENT);
    CHECK(check(&b, KEY, UID_A, NULL) == BIND_EWRITE, "magic programmed wrong silently: halt");
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    CHECK(check(&b, KEY, UID_A, NULL) == BIND_WRITTEN && has_record(&b, KEY, UID_A), "the fault gone: written");
    nor_sim_free(&b.q);
}

// One boot's check; -1 when power is cut during it.
static int boot_once(bench_t *b)
{
    if (setjmp(b->jb))
        return -1;
    return bind_check(&b->q.nor, KEY, UID_A, NULL);
}

// A power cut at every operation of the first-boot write, also with a second cut on the retry:
// the boots after must end bound, never corrupt, and the application is never erased.
static void test_power_cuts(void)
{
    bench_t *b = malloc(sizeof *b);  // changes between setjmp and longjmp (C11 7.13.2.1p3)
    bench_init(b, 30);
    long total;
    check(b, KEY, UID_A, &total);
    nor_sim_free(&b->q);
    int bad = 0;
    for (int cuts = 1; cuts <= 2; cuts++)
        for (long cut = 0; cut < total; cut++) {
            bench_init(b, 40 + (uint32_t)cut * 3 + (uint32_t)cuts);
            b->w.cut_at = cut;
            b->w.cuts_left = cuts - 1;
            b->w.cut_period = 1;
            int r = -1;
            for (int boot = 0; boot < 5 && r != BIND_OK && r != BIND_WRITTEN; boot++)
                r = boot_once(b);
            bool ok = (r == BIND_OK || r == BIND_WRITTEN) && has_record(b, KEY, UID_A) && slot_intact(b);
            if (!ok && bad++ < 5)
                printf("  %d cut(s) from op %ld of %ld -> %d\n", cuts, cut, total, r);
            nor_sim_free(&b->q);
        }
    CHECK(bad == 0, "power cuts during the record write: %d runs end wrong", bad);
    printf("bind: %ld operations to write the record, cut at each (once and twice)\n", total);
    free(b);
}

int main(void)
{
    init_ids();
    test_record();
    test_first_boot();
    test_clone();
    test_corrupt_and_errors();
    test_power_cuts();
    printf("test_bind: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
