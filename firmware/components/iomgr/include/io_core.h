// The IO behaviour of the gateway (spec §6.1 `io`, Plan 07b), the ESP32's iomgr.c as pure C: DI debounce
// and inversion (inputs active low), the DO modes (follow, output hold = pulse, timer flip = blink), the
// six time-of-day timers, and the mask `keepOutputs` saves. Time comes in as arguments; host-tested
// (test_io_core). iomgr.c drives it on the board, under one mutex.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

#define IO_DO 2
#define IO_DI 2
#define IO_TIMERS 6
#define IO_TICK_MS 10u          // the io task's period: a debounce counts this sample as one tick
#define IO_MAX_CATCH_UP_S 120   // a larger clock jump (SNTP, POST /api/time) fires no timer

enum { IO_DO_FOLLOW = 0, IO_DO_PULSE = 1, IO_DO_BLINK = 2 };     // do<i>Mode
enum { IO_ACT_OPEN = 0, IO_ACT_CLOSE = 1, IO_ACT_TOGGLE = 2 };  // timer<i>OutputAction

typedef struct {
    bool enable, restart;  // timer<i>Action 0: restart the gateway, 1: set an output
    int sod;               // second of the day (local time)
    int out, action;
} io_timer_cfg_t;

typedef struct {
    int do_mode[IO_DO];
    uint32_t do_time_ms[IO_DO];  // do<i>FlipDelay, at least 1 s
    bool keep;                   // keepOutputs
    uint32_t di_debounce_ms;
    bool di_invert;
    uint8_t mb_addr;
    io_timer_cfg_t timers[IO_TIMERS];
} io_cfg_t;

/** A DO or DI index from JSON: v when it is a whole number in 0..n-1, else -1 (never truncated). */
int io_index(double v, int n);

/** The `io` config (a missing or malformed timer time disables that timer). */
void io_cfg_parse(const cJSON *io, io_cfg_t *out);

typedef struct {
    io_cfg_t cfg;
    uint8_t out;  // the outputs now, bit i = DO i+1
    bool di[IO_DI];
    bool di_moving[IO_DI];
    uint32_t di_since[IO_DI];
    bool pulsing[IO_DO], rest[IO_DO], blink[IO_DO];
    uint32_t deadline[IO_DO];
    int64_t last_s;  // timers: the last second checked, 0 = none
    uint8_t saved;   // keepOutputs: the mask in the flash
    bool saved_known;
} io_core_t;

/** All outputs off; the DI start as `pins` reads them (bit i = DI i+1's pin level), with no debounce. */
void io_core_init(io_core_t *c, const io_cfg_t *cfg, uint8_t pins);

/** One sample of DI i's pin (true = high = not active). The debounced, inversion-applied state. */
bool io_core_di(io_core_t *c, int i, bool pin, uint32_t now_ms);
bool io_core_di_get(const io_core_t *c, int i);

/** A command for DO i, from the web, MQTT, linkage or a timer. 0, or -1 for no such output. */
int io_core_do_set(io_core_t *c, int i, bool on, uint32_t now_ms);
bool io_core_do_get(const io_core_t *c, int i);
/** Ends pulses and steps blinks whose time has come. */
void io_core_tick(io_core_t *c, uint32_t now_ms);
uint8_t io_core_outputs(const io_core_t *c);
/** What keepOutputs saves: a pulse as its rest state, a blinking output as on. */
uint8_t io_core_commanded(const io_core_t *c);

/** At boot with keepOutputs: `mask` from the flash, `found` false when there was none. */
void io_core_restore(io_core_t *c, uint8_t mask, bool found, uint32_t now_ms);

/**
 * The timers, for every second in (last check, utc_s] (local time = UTC + tz_hours). Output actions
 * are applied here; true when a timer asks for a restart. Nothing fires while the clock is not set,
 * after a clock that went back, or after a jump of more than IO_MAX_CATCH_UP_S.
 */
bool io_core_timers(io_core_t *c, int64_t utc_s, int tz_hours, bool clock_set, uint32_t now_ms);

/** keepOutputs: true with the latest commanded mask when it differs from the saved one. */
bool io_core_persist_take(const io_core_t *c, uint8_t *mask);
/** After the save of `mask`: recorded when ok, else the next take tries again. */
void io_core_persist_done(io_core_t *c, uint8_t mask, bool ok);
