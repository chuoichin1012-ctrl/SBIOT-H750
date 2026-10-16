// A port's network bridge (spec §6.4, Plan 08a); see bridge.c.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

/** Starts port 1..2's bridge from port<n> and system (read once at boot, spec §9.1). 0, or -1 (logged). */
int bridge_start(int port, const cJSON *pcfg, const cJSON *sys);
/** "LISTEN", "CONNECTED(n)", "CONNECTING", "WAITING", "ERROR" or "NOT STARTED". */
void bridge_state(int port, char *out, size_t len);
/** Serial frames dropped because the port's queue was full. */
uint32_t bridge_dropped(int port);
/** Bench: the free netconns a TCP server's listener needs to accept (spec §5.6; default 4). */
void bridge_set_accept_reserve(int n);
