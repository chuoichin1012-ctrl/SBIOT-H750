// The Reload button while the app runs (spec §11.4, Plan 07b): held 3-15 s, then released, restores the
// factory configuration. A button already held at the first sample counts only from the next press.
// Pure C, host-tested (test_reload); sampled on the io task's 10 ms tick.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define RELOAD_MIN_MS 3000u
#define RELOAD_MAX_MS 15000u
#define RELOAD_UP_SAMPLES 2  // a release counts after 2 samples up: a bounce does not split a press

typedef enum { RELOAD_NONE, RELOAD_HINT, RELOAD_FACTORY } reload_ev_t;  // HINT: held 3 s, "release now"

typedef struct {
    bool started, armed, down, hinted;
    int up;               // samples up since the button went up during a press
    uint32_t since, upat;  // press start, first sample up
} reload_t;

void reload_init(reload_t *r);
reload_ev_t reload_step(reload_t *r, bool pressed, uint32_t now_ms);
