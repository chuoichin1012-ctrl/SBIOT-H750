// Host tests for the Reload button (spec §11.4, Plan 07b; the user 2026-10-17: released at 3-10 s restarts,
// at 10-15 s restores the factory settings): make -C stm32/firmware/test/host
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

typedef struct {
    int restart, factory, hint_restart, hint_factory;
} counts_t;

static void count(counts_t *c, reload_ev_t v)
{
    c->restart += v == RELOAD_RESTART;
    c->factory += v == RELOAD_FACTORY;
    c->hint_restart += v == RELOAD_HINT_RESTART;
    c->hint_factory += v == RELOAD_HINT_FACTORY;
}

// Samples every 10 ms: up for 1 s, pressed for hold_ms, up for 1 s. Counts the events.
static void press(reload_t *r, uint32_t *t, uint32_t hold_ms, counts_t *c)
{
    for (uint32_t e = *t + 1000; *t < e; *t += 10)
        count(c, reload_step(r, false, *t));
    for (uint32_t e = *t + hold_ms; *t < e; *t += 10)
        count(c, reload_step(r, true, *t));
    for (uint32_t e = *t + 1000; *t < e; *t += 10)
        count(c, reload_step(r, false, *t));
}

static counts_t run(uint32_t hold_ms)
{
    reload_t r;
    reload_init(&r);
    uint32_t t = 0;
    counts_t c = {0};
    press(&r, &t, hold_ms, &c);
    return c;
}

int main(void)
{
    counts_t c = run(1000);
    CHECK(!c.restart && !c.factory && !c.hint_restart, "1 s: nothing");
    c = run(2990);
    CHECK(!c.restart && !c.factory, "released at 2.99 s: nothing");
    c = run(3000);
    CHECK(c.restart == 1 && !c.factory, "3 s: restart");
    c = run(3500);
    CHECK(c.restart == 1 && c.hint_restart == 1 && !c.hint_factory, "3.5 s: one hint at 3 s, then restart");
    c = run(9990);
    CHECK(c.restart == 1 && !c.factory && !c.hint_factory, "9.99 s: still a restart");
    c = run(10000);
    CHECK(!c.restart && c.factory == 1, "10 s: factory");
    c = run(10500);
    CHECK(!c.restart && c.factory == 1 && c.hint_restart == 1 && c.hint_factory == 1, "10.5 s: the hints at 3 s and 10 s, then factory");
    c = run(15000);
    CHECK(!c.restart && c.factory == 1, "15 s: factory");
    c = run(15010);
    CHECK(!c.restart && !c.factory, "15.01 s: nothing");
    c = run(20000);
    CHECK(!c.restart && !c.factory && c.hint_factory == 1, "20 s: nothing (the hints were given)");
    // Held at the first sample (the bootloader's 5 s restore just ran): ignored until a release.
    reload_t r;
    reload_init(&r);
    uint32_t t = 0;
    c = (counts_t){0};
    for (; t < 6000; t += 10)
        count(&c, reload_step(&r, true, t));
    for (uint32_t e = t + 1000; t < e; t += 10)
        count(&c, reload_step(&r, false, t));
    CHECK(!c.restart && !c.factory, "held from the start for 6 s, released: nothing");
    press(&r, &t, 5000, &c);
    CHECK(c.restart == 1 && !c.factory, "then a 5 s press: restart");
    press(&r, &t, 12000, &c);
    CHECK(c.factory == 1, "then a 12 s press: factory");
    // A bounce of one sample in the middle of a press does not split it.
    reload_init(&r);
    t = 0;
    c = (counts_t){0};
    for (uint32_t e = 1000; t < e; t += 10)
        reload_step(&r, false, t);
    for (uint32_t s0 = t; t < s0 + 12000; t += 10)
        count(&c, reload_step(&r, t != s0 + 5000, t));
    for (uint32_t e = t + 1000; t < e; t += 10)
        count(&c, reload_step(&r, false, t));
    CHECK(c.factory == 1 && !c.restart, "a 10 ms bounce at 5 s of a 12 s press: one press, factory (%d, %d)", c.factory, c.restart);
    printf("test_reload: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
