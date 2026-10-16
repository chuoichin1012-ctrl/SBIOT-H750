// The web server and the WebSocket console (spec §10), on Mongoose (Plan 06b).
#pragma once
#include "cJSON.h"

/**
 * Reads `system` once (webPort, websocketPort, websocketTarget, the login, the hostname: spec §9.1) and
 * starts the `web` task, and the `console` task when websocketTarget is not 0. 0, or -1 (logged).
 */
int webapi_start(const cJSON *system);
#include <stddef.h>
