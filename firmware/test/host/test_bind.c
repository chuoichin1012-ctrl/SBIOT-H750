// Host tests for the anti-clone binding (spec §11.5, §12.1): make -C stm32/firmware/test/host
// The records come from tools/provision.py (the Makefile passes the sample test_provision.py
// writes), so Python's Ed25519 signer and Monocypher's check are proven against each other.
// Simulated QSPI (nor_sim): the bootloader never writes a record; a record of another chip or key
// erases the application slot, also across power cuts; read errors, corrupt and missing records.
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bind.h"
#include "crc32.h"
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

// The sample: dev public key | UID A | record of A | UID B | record of A signed with another key | an old
// (version 1, 84 bytes then 0xFF) record of A.
static uint8_t PUB[BIND_PUB_LEN], UID_A[BIND_UID_LEN], REC_A[BIND_LEN], UID_B[BIND_UID_LEN], REC_OTHER[BIND_LEN],
    REC_V1[BIND_LEN];

static bool load_sample(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    bool ok = fread(PUB, 1, sizeof PUB, f) == sizeof PUB && fread(UID_A, 1, sizeof UID_A, f) == sizeof UID_A &&
              fread(REC_A, 1, sizeof REC_A, f) == sizeof REC_A && fread(UID_B, 1, sizeof UID_B, f) == sizeof UID_B &&
              fread(REC_OTHER, 1, sizeof REC_OTHER, f) == sizeof REC_OTHER && fread(REC_V1, 1, sizeof REC_V1, f) == sizeof REC_V1;
    fclose(f);
    return ok;
}

typedef struct {
    sim_world_t w;
    nor_sim_t q;
    jmp_buf jb;
} bench_t;

// A board with an application in the slot (a pattern that is never 0xFF) and rec at QSPI_BIND (NULL: none).
static void bench_init(bench_t *b, uint32_t seed, const uint8_t *rec)
{
    memset(b, 0, sizeof *b);
    sim_world_init(&b->w, seed);
    b->w.cut_jmp = &b->jb;
    nor_sim_init(&b->q, &b->w, DEV_QSPI, 0x400000);
    for (uint32_t i = 0; i < SLOT_SIZE; i++)
        b->q.mem[QSPI_SLOT + i] = (uint8_t)(i * 7 + 1) & 0x7F;
    if (rec)
        memcpy(b->q.mem + QSPI_BIND, rec, BIND_LEN);
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

static int check(bench_t *b, const uint8_t *uid, long *ops)
{
    long o = b->w.ops;
    int r = bind_check(&b->q.nor, PUB, uid, NULL);
    if (ops)
        *ops = b->w.ops - o;
    return r;
}

// Sets the CRC of a record changed on purpose, the way an attacker would.
static void fix_crc(uint8_t rec[BIND_LEN])
{
    uint32_t c = crc32_update(0, rec, 100);
    memcpy(rec + 100, &c, 4);
}

static void test_valid(void)
{
    CHECK(bind_valid(REC_A, PUB, UID_A), "the app accepts a record from provision.py for its chip");
    CHECK(!bind_valid(REC_A, PUB, UID_B), "and refuses it on another chip");
    CHECK(!bind_valid(REC_OTHER, PUB, UID_A), "and a record signed with another key");
    uint8_t r[BIND_LEN];
    memcpy(r, REC_A, BIND_LEN);
    memcpy(r + 4, UID_B, BIND_UID_LEN);
    fix_crc(r);
    CHECK(!bind_valid(r, PUB, UID_B), "a record whose UID was changed to this chip: the signature fails");
    memcpy(r, REC_A, BIND_LEN);
    r[40] ^= 1;
    fix_crc(r);
    CHECK(!bind_valid(r, PUB, UID_A), "one signature bit changed");
    memcpy(r, REC_A, BIND_LEN);
    r[102] ^= 1;
    CHECK(!bind_valid(r, PUB, UID_A), "a wrong CRC");
    // Plan 11a: the MAC and the SN are signed; the record's UID must be this chip's.
    memcpy(r, REC_A, BIND_LEN);
    r[16 + 5] ^= 1;
    fix_crc(r);
    CHECK(!bind_valid(r, PUB, UID_A), "the MAC changed, CRC fixed: the signature fails");
    memcpy(r, REC_A, BIND_LEN);
    r[22 + 11] = r[22 + 11] == '9' ? '8' : '9';
    fix_crc(r);
    CHECK(!bind_valid(r, PUB, UID_A), "the SN changed, CRC fixed: the signature fails");
    CHECK(!bind_valid(REC_V1, PUB, UID_A), "an old (version 1) record is not valid");
    uint8_t mac[6];
    char sn[13];
    CHECK(bind_identity(REC_A, mac, sn) && mac[0] == 0x02 && !strcmp(sn, "SB2610000001"), "the identity: MAC 02.., SN %s", sn);
    memcpy(r, REC_A, BIND_LEN);
    r[22 + 3] = 'x';
    CHECK(!bind_identity(r, mac, sn), "an SN that is not SB + 10 digits is refused");
    memset(r, 0xFF, BIND_LEN);
    CHECK(!bind_valid(r, PUB, UID_A), "no record");
}

static void test_genuine(void)
{
    bench_t b;
    long ops;
    bench_init(&b, 1, REC_A);
    CHECK(check(&b, UID_A, &ops) == BIND_OK && ops == 0, "provisioned board: OK, nothing written");
    CHECK(slot_intact(&b), "the application is untouched");
    nor_sim_free(&b.q);
}

static void test_not_provisioned(void)
{
    bench_t b;
    long ops;
    bench_init(&b, 2, NULL);
    CHECK(check(&b, UID_A, &ops) == BIND_ENONE && ops == 0, "no record: halt, the bootloader never writes one");
    CHECK(slot_intact(&b) && check(&b, UID_A, &ops) == BIND_ENONE, "the application kept, and the next boot halts again");
    nor_sim_free(&b.q);

    uint8_t r[BIND_LEN];
    memcpy(r, REC_A, BIND_LEN);
    r[2] = 0xFF;  // a magic cut short
    bench_init(&b, 3, r);
    CHECK(check(&b, UID_A, &ops) == BIND_ENONE && ops == 0 && slot_intact(&b), "a broken magic counts as no record");
    nor_sim_free(&b.q);
}

static void test_old_record(void)
{
    bench_t b;
    long ops;
    bench_init(&b, 30, REC_V1);
    CHECK(check(&b, UID_A, &ops) == BIND_EOLD && ops == 0 && slot_intact(&b), "an old record: halt, nothing erased");
    nor_sim_free(&b.q);
}

static void test_clone(void)
{
    struct {
        const uint8_t *rec, *uid;
        const char *what;
    } c[3] = {{REC_A, UID_B, "QSPI copied onto another chip"}, {REC_OTHER, UID_A, "a record signed with another key"}, {NULL, UID_B, "UID rewritten and CRC fixed"}};
    uint8_t forged[BIND_LEN];
    memcpy(forged, REC_A, BIND_LEN);
    memcpy(forged + 4, UID_B, BIND_UID_LEN);
    fix_crc(forged);
    c[2].rec = forged;
    for (int i = 0; i < 3; i++) {
        bench_t b;
        bench_init(&b, 10 + i, c[i].rec);
        CHECK(check(&b, c[i].uid, NULL) == BIND_CLONE, "%s: clone", c[i].what);
        CHECK(slot_erased(&b), "%s: the application slot is erased", c[i].what);
        CHECK(!memcmp(b.q.mem + QSPI_BIND, c[i].rec, BIND_LEN), "%s: the record is kept", c[i].what);
        CHECK(check(&b, c[i].uid, NULL) == BIND_CLONE, "%s: the next boot refuses too", c[i].what);
        nor_sim_free(&b.q);
    }

    bench_t b;  // erase errors on part of the slot do not stop the rest
    bench_init(&b, 13, REC_A);
    sim_fail(&b.w, 0, DEV_QSPI, 0x8000, 0x9000, NOR_EIO);
    CHECK(check(&b, UID_B, NULL) == BIND_CLONE, "clone with a failing sector: still a clone");
    CHECK(b.q.mem[0] == 0xFF && b.q.mem[SLOT_SIZE - 1] == 0xFF && b.q.mem[0x8000] != 0xFF, "every other sector erased");
    nor_sim_free(&b.q);
}

static void test_corrupt_and_errors(void)
{
    bench_t b;
    long ops;
    uint8_t r[BIND_LEN];
    memcpy(r, REC_A, BIND_LEN);
    r[40] ^= 1;  // a signature bit, CRC not fixed: damage, not forgery
    bench_init(&b, 20, r);
    CHECK(check(&b, UID_A, &ops) == BIND_ECORRUPT && ops == 0 && slot_intact(&b), "corrupt record: halt, nothing erased");
    nor_sim_free(&b.q);

    bench_init(&b, 21, REC_A);
    sim_fail(&b.w, 0, DEV_QSPI, QSPI_BIND, QSPI_BIND + NOR_SECTOR, SIM_READ_EIO);
    CHECK(check(&b, UID_B, &ops) == BIND_EREAD && ops == 0 && slot_intact(&b), "record unreadable: halt, nothing erased");
    sim_fail(&b.w, 0, 0, 0, 0, 0);
    CHECK(check(&b, UID_A, &ops) == BIND_OK, "readable again: OK");
    nor_sim_free(&b.q);
}

// One boot's check; -1 when power is cut during it.
static int boot_once(bench_t *b, const uint8_t *uid)
{
    if (setjmp(b->jb))
        return -1;
    return bind_check(&b->q.nor, PUB, uid, NULL);
}

// Power cut at every erase of a clone's slot: the next boot is a clone again, and the slot ends erased.
static void test_power_cuts(void)
{
    bench_t *b = malloc(sizeof *b);  // changes between setjmp and longjmp (C11 7.13.2.1p3)
    bench_init(b, 30, REC_A);
    long total;
    check(b, UID_B, &total);
    nor_sim_free(&b->q);
    int bad = 0;
    for (long cut = 0; cut < total; cut += 7) {
        bench_init(b, 40 + (uint32_t)cut, REC_A);
        b->w.cut_at = cut;
        int first = boot_once(b, UID_B), second = boot_once(b, UID_B);
        bool ok = first == -1 && second == BIND_CLONE && slot_erased(b) && !memcmp(b->q.mem + QSPI_BIND, REC_A, BIND_LEN);
        if (!ok && bad++ < 5)
            printf("  cut at erase %ld of %ld -> %d then %d\n", cut, total, first, second);
        nor_sim_free(&b->q);
    }
    CHECK(bad == 0, "power cuts while erasing a clone: %d runs end wrong", bad);
    printf("bind: %ld erases for a clone, cut at every 7th\n", total);
    free(b);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "build/bind_sample.bin";
    if (!load_sample(path)) {
        printf("FAIL: cannot read %s (tools/test_provision.py writes it)\ntest_bind: FAILED\n", path);
        return 1;
    }
    test_valid();
    test_genuine();
    test_not_provisioned();
    test_old_record();
    test_clone();
    test_corrupt_and_errors();
    test_power_cuts();
    printf("test_bind: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
