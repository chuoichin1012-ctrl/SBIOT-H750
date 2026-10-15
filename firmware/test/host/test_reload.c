// Host tests for the Reload button (spec §11.4, Plan 07b): make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include "reload.h"

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

// Samples every 10 ms: up for 1 s, pressed for hold_ms, up for 1 s. Counts the events.
static void press(reload_t *r, uint32_t *t, uint32_t hold_ms, int *factory, int *hints)
{
    for (uint32_t e = *t + 1000; *t < e; *t += 10)
        *factory += reload_step(r, false, *t) == RELOAD_FACTORY;
    for (uint32_t e = *t + hold_ms; *t < e; *t += 10) {
        reload_ev_t v = reload_step(r, true, *t);
        *factory += v == RELOAD_FACTORY;
        *hints += v == RELOAD_HINT;
    }
    for (uint32_t e = *t + 1000; *t < e; *t += 10)
        *factory += reload_step(r, false, *t) == RELOAD_FACTORY;
}

static int run(uint32_t hold_ms, int *hints)
{
    reload_t r;
    reload_init(&r);
    uint32_t t = 0;
    int f = 0, h = 0;
    press(&r, &t, hold_ms, &f, &h);
    if (hints)
        *hints = h;
    return f;
}

int main(void)
{
    int h;
    CHECK(run(1000, &h) == 0 && h == 0, "1 s: nothing");
    CHECK(run(2990, NULL) == 0, "released at 2.99 s: nothing");
    CHECK(run(3000, NULL) == 1, "3 s: factory");
    CHECK(run(3500, &h) == 1 && h == 1, "3.5 s: one hint at 3 s, then factory");
    CHECK(run(15000, NULL) == 1, "15 s: factory");
    CHECK(run(15010, NULL) == 0, "15.01 s: nothing");
    CHECK(run(20000, NULL) == 0, "20 s: nothing");
    // Held at the first sample (the bootloader's 5 s restore just ran): ignored until a release.
    reload_t r;
    reload_init(&r);
    uint32_t t = 0;
    int f = 0, hh = 0;
    for (; t < 6000; t += 10)
        f += reload_step(&r, true, t) == RELOAD_FACTORY;
    for (uint32_t e = t + 1000; t < e; t += 10)
        f += reload_step(&r, false, t) == RELOAD_FACTORY;
    CHECK(f == 0, "held from the start for 6 s, released: nothing");
    press(&r, &t, 5000, &f, &hh);
    CHECK(f == 1, "then a 5 s press: factory");
    // A bounce of one sample in the middle of a 5 s press does not split it.
    reload_init(&r);
    t = 0;
    f = 0;
    for (uint32_t e = 1000; t < e; t += 10)
        reload_step(&r, false, t);
    for (uint32_t s = t; t < s + 5000; t += 10)
        f += reload_step(&r, t != s + 2000, t) == RELOAD_FACTORY;
    for (uint32_t e = t + 1000; t < e; t += 10)
        f += reload_step(&r, false, t) == RELOAD_FACTORY;
    CHECK(f == 1, "a 10 ms bounce at 2 s of a 5 s press: one press, factory (%d)", f);
    printf("test_reload: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
