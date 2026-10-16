// The Edge runtime (spec §9, Plan 08c); see edge_rt.c.
#pragma once
#include <stdbool.h>
#include <stddef.h>

/** Loads the saved model and starts Edge, all or nothing. From services_start, after the bridges and
 * before webapi_start (it borrows the web's Edge text buffer). Off with a status when memory runs out. */
void edge_start(void);
/** /api/status edge.state: "NOT RUNNING", "NO MEMORY", "RUNNING ok/total", with notes. */
/** Edge was on at boot (edge_start ran): what /api/status reports until the restart (Plan 10c, Codex Minor 12). */
bool edge_enabled(void);
void edge_status(char *state, size_t len);
