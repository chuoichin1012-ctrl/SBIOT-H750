// LED WORK rhythm (Plan 11a task 0, the user): LED WORK is WDI (PC10, SGM706 through Q6), so it toggles at every
// WDI kick. Half periods: 1 s blink when running, 0.2 s in an OTA, an install or a restore, 0.5 s halted. Each
// stays well inside the SGM706's minimum timeout of 1.0 s (datasheet p4: 1.0 / 1.6 / 2.25 s).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PACE_RUN_MS 500u
#define PACE_BUSY_MS 100u
#define PACE_HALT_MS 250u

/** True, and `*last_ms` moved to `now_ms`, when `half_ms` has passed since `*last_ms` (wrap-safe). */
bool pace_due(uint32_t now_ms, uint32_t *last_ms, uint32_t half_ms);
