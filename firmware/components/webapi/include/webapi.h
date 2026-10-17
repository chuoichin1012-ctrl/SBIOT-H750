// The web server and the WebSocket console (spec §10), on Mongoose (Plan 06b).
#pragma once
#include <stdint.h>
#include "cJSON.h"

/**
 * Reads `system` once (webPort, websocketPort, websocketTarget, the login, the hostname: spec §9.1) and
 * starts the `web` task, and the `console` task when websocketTarget is not 0. 0, or -1 (logged).
 */
/** Bench (console I, Plan 11a task 0b): the web task stops for `ms`, so the supervisor restarts the board. */
void web_bench_hang(uint32_t ms);
int webapi_start(const cJSON *system);
#include <stddef.h>
