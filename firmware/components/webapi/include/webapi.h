// The web server and the WebSocket console (spec §10), on Mongoose (Plan 06b).
#pragma once
#include "cJSON.h"

/**
 * Reads `system` once (webPort, websocketPort, websocketTarget, the login, the hostname: spec §9.1) and
 * starts the `web` task, and the `console` task when websocketTarget is not 0. 0, or -1 (logged).
 */
int webapi_start(const cJSON *system);
#include <stddef.h>
/** The Edge text buffer (48 KB + 1, spec §9.3) for the boot's model load (Plan 08c): init only, before
 * webapi_start, which then owns it (asserted). *cap is its size. */
char *web_edge_buffer_for_boot(size_t *cap);
