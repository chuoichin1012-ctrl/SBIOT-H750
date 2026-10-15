// Host tests for the IO logic (spec §6.1 `io`, Plan 07b): make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "io_core.h"

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

static io_cfg_t cfg_of(const char *json)
{
    cJSON *j = cJSON_Parse(json);
    io_cfg_t c;
    io_cfg_parse(j, &c);
    cJSON_Delete(j);
    return c;
}

// The factory io config with some fields changed.
static const char *BASE =
    "{\"modbusAddress\":100,\"keepOutputs\":1,\"do0Mode\":0,\"do0FlipDelay\":5,\"do1Mode\":0,\"do1FlipDelay\":5,"
    "\"diDebounce\":50,\"diInvert\":0}";

// ---- config ---------------------------------------------------------------------------------------

static void test_cfg(void)
{
    io_cfg_t c = cfg_of("{\"modbusAddress\":7,\"keepOutputs\":1,\"do0Mode\":1,\"do0FlipDelay\":2,\"do1Mode\":2,"
                        "\"do1FlipDelay\":0,\"diDebounce\":30,\"diInvert\":1,"
                        "\"timer0Enabled\":1,\"timer0Time\":\"08:30\",\"timer0Action\":1,\"timer0Output\":1,\"timer0OutputAction\":2,"
                        "\"timer1Enabled\":1,\"timer1Time\":\"23:59:59\",\"timer1Action\":0,"
                        "\"timer2Enabled\":1,\"timer2Time\":\"24:00\",\"timer3Enabled\":1,\"timer3Time\":\"bad\"}");
    CHECK(c.mb_addr == 7 && c.keep && c.di_invert && c.di_debounce_ms == 30, "modbusAddress, keepOutputs, diInvert, diDebounce");
    CHECK(c.do_mode[0] == IO_DO_PULSE && c.do_time_ms[0] == 2000 && c.do_mode[1] == IO_DO_BLINK && c.do_time_ms[1] == 1000,
          "DO modes; a FlipDelay under 1 s is 1 s (%u)", (unsigned)c.do_time_ms[1]);
    CHECK(c.timers[0].enable && !c.timers[0].restart && c.timers[0].sod == 8 * 3600 + 30 * 60 && c.timers[0].out == 1 &&
              c.timers[0].action == IO_ACT_TOGGLE,
          "timer 0: 08:30, set output 2, toggle");
    CHECK(c.timers[1].enable && c.timers[1].restart && c.timers[1].sod == 86399, "timer 1: 23:59:59, restart");
    CHECK(!c.timers[2].enable && !c.timers[3].enable && !c.timers[4].enable, "a malformed time disables its timer");
}

// ---- DI -------------------------------------------------------------------------------------------

static void test_di(void)
{
    io_cfg_t cfg = cfg_of(BASE);
    io_core_t c;
    io_core_init(&c, &cfg, 0x3);  // both pins high: inactive (active low)
    CHECK(!io_core_di_get(&c, 0) && !io_core_di_get(&c, 1), "pins high: inactive");
    uint32_t t = 1000;
    int switched_at = -1;
    for (int k = 0; k < 10; k++, t += IO_TICK_MS)
        if (io_core_di(&c, 0, false, t) && switched_at < 0)
            switched_at = k;
    CHECK(switched_at == 4, "a low pin turns DI1 active after 50 ms of debounce: 5 samples (%d)", switched_at + 1);
    // A bounce shorter than the debounce changes nothing.
    io_core_di(&c, 0, true, t);
    io_core_di(&c, 0, true, t + 10);
    io_core_di(&c, 0, false, t + 20);
    CHECK(io_core_di_get(&c, 0), "a 20 ms bounce: still active");
    // diInvert
    cfg = cfg_of("{\"diDebounce\":10,\"diInvert\":1}");
    io_core_init(&c, &cfg, 0x3);
    CHECK(io_core_di_get(&c, 0), "diInvert: a high pin is active");
    CHECK(!io_core_di(&c, 0, false, 5000), "diDebounce 10: one sample switches (as on the ESP32)");
    // A debounce under one tick acts as one tick, never zero.
    cfg = cfg_of("{\"diDebounce\":1,\"diInvert\":0}");
    io_core_init(&c, &cfg, 0x3);
    CHECK(io_core_di(&c, 1, false, 100), "diDebounce 1: switches at the first sample");
    CHECK(io_core_di(&c, 1, false, 110) && io_core_di_get(&c, 1), "and stays");
}

// ---- DO -------------------------------------------------------------------------------------------

static void test_do(void)
{
    io_cfg_t cfg = cfg_of("{\"keepOutputs\":1,\"do0Mode\":1,\"do0FlipDelay\":2,\"do1Mode\":2,\"do1FlipDelay\":1}");
    io_core_t c;
    io_core_init(&c, &cfg, 0x3);
    CHECK(io_core_do_set(&c, 2, true, 0) == -1 && io_core_do_set(&c, -1, true, 0) == -1, "no DO3, no DO0");
    // Mode 1 (output hold): on for 2 s, then back to the rest state.
    io_core_do_set(&c, 0, true, 1000);
    CHECK(io_core_do_get(&c, 0) && io_core_commanded(&c) == 0, "pulse: on now; commanded is the rest state (off)");
    io_core_tick(&c, 2999);
    CHECK(io_core_do_get(&c, 0), "still on at 1.999 s");
    io_core_tick(&c, 3000);
    CHECK(!io_core_do_get(&c, 0), "back off at 2 s");
    // Commanding the pulsed state again restarts its time; commanding the rest state ends it.
    io_core_do_set(&c, 0, true, 10000);
    io_core_do_set(&c, 0, true, 11500);
    io_core_tick(&c, 12000);
    CHECK(io_core_do_get(&c, 0), "a re-command at 1.5 s restarts the 2 s");
    io_core_tick(&c, 13500);
    CHECK(!io_core_do_get(&c, 0), "and ends 2 s after it");
    io_core_do_set(&c, 0, true, 20000);
    io_core_do_set(&c, 0, false, 20500);
    io_core_tick(&c, 30000);
    CHECK(!io_core_do_get(&c, 0) && !c.pulsing[0], "commanding the rest state ends the pulse");
    // Mode 2 (timer flip): toggles every 1 s while commanded on.
    io_core_do_set(&c, 1, true, 0);
    CHECK(io_core_do_get(&c, 1) && io_core_commanded(&c) == 0x2, "blink: on; commanded on");
    io_core_tick(&c, 999);
    CHECK(io_core_do_get(&c, 1), "on until 1 s");
    io_core_tick(&c, 1000);
    CHECK(!io_core_do_get(&c, 1) && io_core_commanded(&c) == 0x2, "off at 1 s, still commanded on");
    io_core_tick(&c, 2000);
    CHECK(io_core_do_get(&c, 1), "on at 2 s");
    io_core_tick(&c, 9500);  // a stalled task: no burst of toggles, the period goes on from now
    io_core_tick(&c, 9600);
    bool after_stall = io_core_do_get(&c, 1);
    io_core_tick(&c, 10500);
    CHECK(io_core_do_get(&c, 1) != after_stall, "after a stall, the next toggle is one period later");
    io_core_do_set(&c, 1, false, 11000);
    io_core_tick(&c, 20000);
    CHECK(!io_core_do_get(&c, 1) && io_core_commanded(&c) == 0, "off stops the blink and stays off");
    // Mode 0 follows.
    cfg = cfg_of(BASE);
    io_core_init(&c, &cfg, 0x3);
    io_core_do_set(&c, 1, true, 0);
    io_core_tick(&c, 100000);
    CHECK(io_core_outputs(&c) == 0x2 && io_core_commanded(&c) == 0x2, "mode 0: follows");
}

// ---- keepOutputs ----------------------------------------------------------------------------------

static void test_persist(void)
{
    io_cfg_t cfg = cfg_of("{\"keepOutputs\":1,\"do0Mode\":0,\"do1Mode\":2,\"do1FlipDelay\":1}");
    io_core_t c;
    uint8_t m;
    io_core_init(&c, &cfg, 0x3);
    io_core_restore(&c, 0x2, true, 0);
    CHECK(io_core_outputs(&c) == 0x2 && c.blink[1], "restore: DO2 in blink mode resumes blinking");
    CHECK(!io_core_persist_take(&c, &m), "nothing to save right after the restore");
    io_core_do_set(&c, 0, true, 10);
    io_core_do_set(&c, 0, false, 20);
    CHECK(!io_core_persist_take(&c, &m), "on then off before a save: back to the saved mask, no save");
    io_core_do_set(&c, 0, true, 30);
    CHECK(io_core_persist_take(&c, &m) && m == 0x3, "a change: save 0x3");
    io_core_do_set(&c, 0, false, 40);  // during the save
    io_core_persist_done(&c, 0x3, true);
    CHECK(io_core_persist_take(&c, &m) && m == 0x2, "a change during a save leads to a second save of the latest (0x2)");
    io_core_persist_done(&c, 0x2, false);
    CHECK(io_core_persist_take(&c, &m) && m == 0x2, "a failed save is tried again");
    io_core_persist_done(&c, 0x2, true);
    CHECK(!io_core_persist_take(&c, &m), "saved");
    io_core_tick(&c, 5000);  // blinking never needs a save
    CHECK(!io_core_persist_take(&c, &m), "a blink step needs no save");
    // No record in the flash yet: the first take saves.
    io_core_init(&c, &cfg, 0x3);
    io_core_restore(&c, 0, false, 0);
    CHECK(io_core_persist_take(&c, &m) && m == 0, "no saved mask yet: save the current one");
    // keepOutputs 0: never saves.
    cfg = cfg_of(BASE);
    cfg.keep = false;
    io_core_init(&c, &cfg, 0x3);
    io_core_do_set(&c, 0, true, 0);
    CHECK(!io_core_persist_take(&c, &m), "keepOutputs 0: no save");
}

// ---- timers ---------------------------------------------------------------------------------------

#define DAY0 1767225600  // 2026-01-01 00:00:00 UTC

static void test_timers(void)
{
    io_cfg_t cfg = cfg_of("{\"keepOutputs\":0,"
                          "\"timer0Enabled\":1,\"timer0Time\":\"08:00:00\",\"timer0Action\":1,\"timer0Output\":0,\"timer0OutputAction\":1,"
                          "\"timer1Enabled\":1,\"timer1Time\":\"08:00:05\",\"timer1Action\":1,\"timer1Output\":0,\"timer1OutputAction\":0,"
                          "\"timer2Enabled\":1,\"timer2Time\":\"08:00:10\",\"timer2Action\":1,\"timer2Output\":1,\"timer2OutputAction\":2,"
                          "\"timer3Enabled\":1,\"timer3Time\":\"23:59:59\",\"timer3Action\":0}");
    io_core_t c;
    io_core_init(&c, &cfg, 0x3);
    int tz = 7;
    int64_t t0 = DAY0 + 8 * 3600 - tz * 3600;  // 08:00:00 local
    CHECK(!io_core_timers(&c, t0 - 1, tz, true, 0), "the first check only starts the count");
    io_core_timers(&c, t0, tz, true, 0);
    CHECK(io_core_do_get(&c, 0), "08:00:00 close: DO1 on (applied in the core)");
    io_core_timers(&c, t0 + 4, tz, true, 0);
    CHECK(io_core_do_get(&c, 0), "nothing in between");
    io_core_timers(&c, t0 + 7, tz, true, 0);  // a stall over 08:00:05
    CHECK(!io_core_do_get(&c, 0), "08:00:05 open, caught up after a stall");
    io_core_timers(&c, t0 + 10, tz, true, 0);
    CHECK(io_core_do_get(&c, 1), "08:00:10 toggle: DO2 on");
    // A clock that goes back fires nothing twice.
    io_core_timers(&c, t0 + 5, tz, true, 0);
    io_core_timers(&c, t0 + 11, tz, true, 0);
    CHECK(!io_core_do_get(&c, 0) && io_core_do_get(&c, 1), "after the clock went back to 08:00:05: nothing fired again");
    // A jump of more than 120 s fires nothing.
    io_core_init(&c, &cfg, 0x3);
    io_core_timers(&c, t0 - 200, tz, true, 0);
    io_core_timers(&c, t0 + 1, tz, true, 0);
    CHECK(!io_core_do_get(&c, 0), "a 201 s jump over 08:00:00: nothing");
    // The clock not set: nothing, and the count starts over.
    io_core_init(&c, &cfg, 0x3);
    io_core_timers(&c, t0 - 1, tz, true, 0);
    io_core_timers(&c, t0, tz, false, 0);
    io_core_timers(&c, t0 + 1, tz, true, 0);
    CHECK(!io_core_do_get(&c, 0), "the clock unset over 08:00:00: nothing");
    // 23:59:59 across midnight: one restart request.
    io_core_init(&c, &cfg, 0x3);
    int64_t mid = DAY0 + 86400 - tz * 3600;  // 00:00:00 local, the next day
    int restarts = 0;
    for (int64_t s = mid - 3; s <= mid + 3; s++)
        restarts += io_core_timers(&c, s, tz, true, 0);
    CHECK(restarts == 1, "23:59:59: one restart (%d)", restarts);
    // A negative zone wraps the day right.
    io_core_init(&c, &cfg, 0x3);
    int64_t t8 = DAY0 + 8 * 3600 + 5 * 3600;  // 08:00:00 at UTC-5
    io_core_timers(&c, t8 - 1, -5, true, 0);
    io_core_timers(&c, t8, -5, true, 0);
    CHECK(io_core_do_get(&c, 0), "UTC-5: 08:00:00 local");
}

int main(void)
{
    test_cfg();
    test_di();
    test_do();
    test_persist();
    test_timers();
    printf("test_io_core: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
