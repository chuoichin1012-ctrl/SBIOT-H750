// The Reload button while the app runs (spec §11.4, Plan 07b; the user 2026-10-17): held, then released, at
// 3 s up to 10 s restarts the gateway; at 10-15 s restores the factory configuration; shorter or longer does
// nothing. A button already held at the first sample counts only from the next press.
// Pure C, host-tested (test_reload); sampled on the io task's 10 ms tick.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define RELOAD_RESTART_MS 3000u   // from here a release restarts
#define RELOAD_FACTORY_MS 10000u  // from here a release restores the factory settings
#define RELOAD_MAX_MS 15000u      // past this, a release does nothing
#define RELOAD_UP_SAMPLES 2  // a release counts after 2 samples up: a bounce does not split a press

// HINT_*: the press has just reached 3 s ("release now to restart") or 10 s ("release now to restore").
typedef enum { RELOAD_NONE, RELOAD_HINT_RESTART, RELOAD_HINT_FACTORY, RELOAD_RESTART, RELOAD_FACTORY } reload_ev_t;

typedef struct {
    bool started, armed, down, hinted, hinted_factory;
    int up;               // samples up since the button went up during a press
    uint32_t since, upat;  // press start, first sample up
} reload_t;

void reload_init(reload_t *r);
reload_ev_t reload_step(reload_t *r, bool pressed, uint32_t now_ms);
