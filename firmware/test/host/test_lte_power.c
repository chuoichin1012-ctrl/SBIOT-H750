// lte_power.c (Plan 10 T2, roadmap gate "Modem ON/OFF/UNKNOWN and the 2.5 s PWRKEY pulse"): the start
// sequence against fake modems, every pulse length, and the supervision decisions. make -C stm32/firmware/test/host
#include <stdio.h>
#include <string.h>
#include "lte_power.h"

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

// A fake module: whether it is powered, whether it answers, and what wakes it.
typedef enum { M_ON_OK, M_ON_HUNG_UNTIL_RESET, M_OFF, M_DEAD } kind_t;
typedef struct {
    kind_t kind;
    bool powered, answering;
    char trace[256];  // "A3000+ R200 A10000+ K2500 A10000-": what was done, + answered, - silent
    uint32_t longest_pwrkey;
    bool not_responding;
} fake_t;

static void add(fake_t *f, const char *s) { strncat(f->trace, s, sizeof f->trace - strlen(f->trace) - 1); }

static bool at_ok(void *ctx, uint32_t window_ms)
{
    fake_t *f = ctx;
    char b[24];
    snprintf(b, sizeof b, "%sA%u%c", f->trace[0] ? " " : "", (unsigned)window_ms, f->answering ? '+' : '-');
    add(f, b);
    return f->answering;
}

static void pulse(void *ctx, int pin, uint32_t ms)
{
    fake_t *f = ctx;
    char b[24];
    snprintf(b, sizeof b, "%s%c%u", f->trace[0] ? " " : "", pin == LTE_PIN_PWRKEY ? 'K' : 'R', (unsigned)ms);
    add(f, b);
    if (pin == LTE_PIN_RESET) {
        if (f->powered && f->kind != M_DEAD)
            f->answering = true;  // a powered module restarts and answers
    } else {
        if (ms > f->longest_pwrkey)
            f->longest_pwrkey = ms;
        if (ms >= 3000 && f->powered) {
            f->powered = f->answering = false;  // what must never happen
        } else if (ms >= 2000 && !f->powered && f->kind != M_DEAD) {
            f->powered = f->answering = true;
        }
    }
}

static void lg(void *ctx, const char *msg)
{
    fake_t *f = ctx;
    if (strstr(msg, "MODEM NOT RESPONDING"))
        f->not_responding = true;
}

static fake_t make(kind_t k)
{
    fake_t f;
    memset(&f, 0, sizeof f);
    f.kind = k;
    f.powered = k == M_ON_OK || k == M_ON_HUNG_UNTIL_RESET;
    f.answering = k == M_ON_OK;
    return f;
}

static lte_power_t run(fake_t *f, lte_power_t from)
{
    lte_hw_t hw = {at_ok, pulse, lg, f};
    return lte_power_up(&hw, from);
}

static void test_paths(void)
{
    fake_t f = make(M_ON_OK);
    CHECK(run(&f, LTE_UNKNOWN) == LTE_ON && !strcmp(f.trace, "A3000+"), "on and answering: AT in 3 s, nothing else (%s)", f.trace);

    f = make(M_ON_HUNG_UNTIL_RESET);
    CHECK(run(&f, LTE_UNKNOWN) == LTE_ON && !strcmp(f.trace, "A3000- R200 A10000+"), "on but silent: RESET_N 200 ms (%s)", f.trace);
    CHECK(f.powered && f.longest_pwrkey == 0, "and no PWRKEY on a running module");

    f = make(M_OFF);
    CHECK(run(&f, LTE_UNKNOWN) == LTE_ON && !strcmp(f.trace, "A3000- R200 A10000- K2500 A10000+"),
          "off: RESET_N first (harmless), then PWRKEY 2.5 s (%s)", f.trace);

    f = make(M_OFF);
    CHECK(run(&f, LTE_OFF) == LTE_ON && !strcmp(f.trace, "K2500 A10000+"), "from a confirmed OFF: PWRKEY alone (%s)", f.trace);

    f = make(M_DEAD);
    CHECK(run(&f, LTE_UNKNOWN) == LTE_UNKNOWN && f.not_responding && !strcmp(f.trace, "A3000- R200 A10000- K2500 A10000-"),
          "never answering: one round, MODEM NOT RESPONDING, UNKNOWN (%s)", f.trace);
    f = make(M_DEAD);
    CHECK(run(&f, LTE_OFF) == LTE_UNKNOWN && f.not_responding && !strcmp(f.trace, "K2500 A10000- R200 A10000-"),
          "from OFF, never answering: PWRKEY, then RESET_N, then UNKNOWN (%s)", f.trace);
}

static void test_pulses(void)
{
    // every kind, every start state, several rounds: no PWRKEY of 3 s or more, and a running module stays on
    const kind_t kinds[] = {M_ON_OK, M_ON_HUNG_UNTIL_RESET, M_OFF, M_DEAD};
    int bad = 0;
    for (int k = 0; k < 4; k++)
        for (int from = LTE_UNKNOWN; from <= LTE_OFF; from++) {
            fake_t f = make(kinds[k]);
            bool was_on = f.powered;
            for (int round = 0; round < 3; round++)
                run(&f, (lte_power_t)from);
            bad += f.longest_pwrkey >= LTE_PWRKEY_LIMIT_MS || (was_on && !f.powered);
        }
    CHECK(bad == 0, "no pulse of 3 s or more, no running module powered off (%d bad)", bad);
    CHECK(LTE_PWRKEY_ON_MS >= 2000 && LTE_PWRKEY_ON_MS < LTE_PWRKEY_LIMIT_MS, "2.5 s is between 2 s and 3 s");
}

static void test_supervise(void)
{
    const uint32_t T = 1000000;
    CHECK(lte_supervise(T, T - 1000, T - 1000, true, false, LTE_NO_ADDR_MS) == LTE_ACT_NONE, "all well: nothing");
    CHECK(lte_supervise(T, T - 120000, T - 1000, true, false, LTE_NO_ADDR_MS) == LTE_ACT_CFUN, "2 minutes without an address: CFUN");
    CHECK(lte_supervise(T, T - 119999, T - 1000, true, false, LTE_NO_ADDR_MS) == LTE_ACT_NONE, "not yet 2 minutes");
    CHECK(lte_supervise(T, T - 1000, T - 60000, false, false, LTE_NO_ADDR_MS) == LTE_ACT_RECOVER, "60 s without a frame, AT silent: RECOVER");
    CHECK(lte_supervise(T, T - 1000, T - 60000, true, false, LTE_NO_ADDR_MS) == LTE_ACT_NONE, "60 s without a frame, AT answering: nothing");
    CHECK(lte_supervise(T, T - 500000, T - 1000, true, true, LTE_NO_ADDR_MS) == LTE_ACT_NONE, "no SIM, the module answering: no CFUN, no RECOVER");
    CHECK(lte_supervise(T, T - 500000, T - 60000, false, true, LTE_NO_ADDR_MS) == LTE_ACT_RECOVER, "no SIM, the module silent 60 s: RECOVER still (Codex code review)");
}

static void test_backoff(void)
{
    lte_backoff_t b = {0};
    const uint32_t min = 60000;
    uint32_t limits[5];
    lte_action_t acts[5];
    for (int i = 0; i < 5; i++) {
        limits[i] = lte_backoff_limit(&b);
        acts[i] = lte_backoff_step(&b);
    }
    CHECK(limits[0] == 2 * min && limits[1] == 5 * min && limits[2] == 10 * min && limits[3] == 30 * min && limits[4] == 30 * min,
          "the waits grow: 2, 5, 10, 30, 30 minutes (%u %u %u %u %u)", (unsigned)(limits[0] / min), (unsigned)(limits[1] / min),
          (unsigned)(limits[2] / min), (unsigned)(limits[3] / min), (unsigned)(limits[4] / min));
    CHECK(acts[0] == LTE_ACT_CFUN && acts[1] == LTE_ACT_CFUN && acts[2] == LTE_ACT_POWER_CYCLE && acts[3] == LTE_ACT_POWER_CYCLE &&
              acts[4] == LTE_ACT_POWER_CYCLE,
          "CFUN twice, then the soft power cycle");
    lte_backoff_ok(&b);
    CHECK(b.fails == 0 && lte_backoff_limit(&b) == 2 * min && lte_backoff_step(&b) == LTE_ACT_CFUN, "an address: back to 2 minutes and CFUN");
    // lte_supervise with the backoff's limit
    const uint32_t T = 10000000;
    CHECK(lte_supervise(T, T - 5 * min, T - 1000, true, false, 10 * min) == LTE_ACT_NONE, "5 minutes without an address, a 10-minute limit: wait");
    CHECK(lte_supervise(T, T - 10 * min, T - 1000, true, false, 10 * min) == LTE_ACT_CFUN, "10 minutes: the limit reached");
    CHECK(lte_supervise(T, T - 1000, T - 60000, false, false, 30 * min) == LTE_ACT_RECOVER, "a silent module: RECOVER at once, whatever the limit");
}

int main(void)
{
    test_paths();
    test_backoff();
    test_pulses();
    test_supervise();
    printf("test_lte_power: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
