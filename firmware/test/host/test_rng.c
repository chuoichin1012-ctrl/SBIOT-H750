// Host tests for the RNG error checks (rng_core, RM0433 §34.3.7): make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rng_core.h"

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

// A scripted RNG: every SR read takes the next status of the script (the last one repeats), every
// DR read returns the next counter value. clear_seis clears SEIS in the statuses still to come only
// when `clears` says the hardware recovered.
typedef struct {
    uint32_t script[64];
    int n, at;
    uint32_t next_word;
    int dr_reads, clears;
    int recovers;  // 1: clearing SEIS ends the seed error
    int no_drdy_until;  // DRDY reads 0 until this many DR reads (a stopped RNG during recovery)
} fake_t;

static uint32_t f_sr(void *ctx)
{
    fake_t *f = ctx;
    uint32_t v = f->script[f->at < f->n ? f->at : f->n - 1];
    if (f->at < f->n)
        f->at++;
    if (f->dr_reads < f->no_drdy_until)
        v &= ~RNG_DRDY;
    return v;
}

static uint32_t f_dr(void *ctx)
{
    fake_t *f = ctx;
    f->dr_reads++;
    return f->next_word++;
}

static void f_clear(void *ctx)
{
    fake_t *f = ctx;
    f->clears++;
    if (f->recovers)  // from now on, the repeated last status included
        for (int i = f->at < f->n ? f->at : f->n - 1; i < f->n; i++)
            f->script[i] &= ~(RNG_SEIS | RNG_SECS);
}

static fake_t fake(const uint32_t *script, int n)
{
    fake_t f;
    memset(&f, 0, sizeof f);
    memcpy(f.script, script, (size_t)n * sizeof script[0]);
    f.n = n;
    f.next_word = 100;
    f.recovers = 1;
    return f;
}

static int word(fake_t *f, uint32_t *w)
{
    rng_io_t io = {.sr = f_sr, .dr = f_dr, .clear_seis = f_clear, .ctx = f, .spins = 50};
    return rng_word(&io, w);
}

int main(void)
{
    uint32_t w;
    {
        const uint32_t s[] = {0, 0, RNG_DRDY, RNG_DRDY};
        fake_t f = fake(s, 4);
        w = 7;
        CHECK(word(&f, &w) == 0 && w == 100 && f.dr_reads == 1, "a good word (w %u)", (unsigned)w);
    }
    {
        // A seed error already there: recovered (SEIS cleared, 12 words dropped), then a good word.
        uint32_t s[40];
        s[0] = RNG_SECS | RNG_SEIS | RNG_DRDY;
        for (int i = 1; i < 40; i++)
            s[i] = RNG_SECS | RNG_SEIS | RNG_DRDY;
        fake_t f = fake(s, 40);
        CHECK(word(&f, &w) == 0 && f.clears == 1 && f.dr_reads == 13 && w == 112,
              "a seed error: recovered, 12 words dropped, the 13th taken (reads %d, w %u)", f.dr_reads, (unsigned)w);
    }
    {
        // RM0433 §34.3.7: the 12 reads that clean the pipeline do not wait for DRDY, which a stopped
        // RNG does not raise (Codex 06a review r2).
        const uint32_t s[] = {RNG_SECS | RNG_SEIS | RNG_DRDY};
        fake_t f = fake(s, 1);
        f.no_drdy_until = 12;
        CHECK(word(&f, &w) == 0 && f.dr_reads == 13 && w == 112,
              "a seed error with DRDY low: 12 reads without waiting, then a word (reads %d)", f.dr_reads);
    }
    {
        // The seed error stays after the recovery: no word.
        const uint32_t s[] = {RNG_SECS | RNG_SEIS | RNG_DRDY};
        fake_t f = fake(s, 1);
        f.recovers = 0;
        w = 7;
        CHECK(word(&f, &w) == -1 && w == 7, "a seed error that stays: -1, w untouched");
    }
    {
        // The error shows together with DRDY, after the word was read: that word is dropped.
        uint32_t s[40] = {RNG_DRDY, RNG_DRDY | RNG_SEIS | RNG_SECS};
        for (int i = 2; i < 40; i++)
            s[i] = RNG_DRDY | RNG_SEIS | RNG_SECS;
        fake_t f = fake(s, 40);
        CHECK(word(&f, &w) == 0 && w != 100 && f.clears == 1, "an error with DRDY: the word read is dropped (w %u)", (unsigned)w);
    }
    {
        const uint32_t s[] = {RNG_CECS | RNG_DRDY};
        fake_t f = fake(s, 1);
        w = 7;
        CHECK(word(&f, &w) == -1 && w == 7 && f.dr_reads == 0, "a clock error: -1, nothing read");
    }
    {
        const uint32_t s[] = {0};
        fake_t f = fake(s, 1);
        w = 7;
        CHECK(word(&f, &w) == -1 && w == 7, "DRDY never: -1");
    }
    printf("test_rng: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
