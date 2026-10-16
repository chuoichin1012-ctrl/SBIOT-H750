// The Edge runtime (spec §9, Plan 08c); see edge_rt.c.
#pragma once
#include <stdbool.h>
#include <stddef.h>

/** Loads the saved model and starts Edge, all or nothing. From services_start, after the bridges and
 * before webapi_start (it borrows the web's Edge text buffer). Off with a status when memory runs out. */
void edge_start(void);
/** /api/status edge.state: "NOT RUNNING", "NO MEMORY", "RUNNING ok/total", with notes. */
void edge_status(char *state, size_t len);
/** The save check of edgeReport (spec §9.4): false when the template, compiled against the running model's
 * names, needs a report buffer over the 26 KB limit. True when Edge does not run (the boot decides). */
bool edge_template_fits(const char *tmpl, bool quote, const char *placeholder);
