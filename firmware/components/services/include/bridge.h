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

// ---- Edge (Plan 08c) ----------------------------------------------------------------------------
#include <stdbool.h>

/** A report to port n's peers as one frame, no registration prefix (any size: it does not go through the
 * serial queue). Waits until it went to every connection or they were lost; a report not taken within
 * 10 s is withdrawn. False at once: the bridge does not run, it is a Modbus TCP client, or it has no peer
 * and does not dial on demand (report_accept). `data` stays untouched until it returns. */
bool bridge_send(int port, const uint8_t *data, size_t len);
/** Edge's answer to a request (the reply's length), or 0: not Edge's, it goes on down the bridge's path. */
typedef size_t (*bridge_edge_fn)(const uint8_t *req, size_t len, uint8_t *reply, size_t max);
enum { BRIDGE_EDGE_RTU, BRIDGE_EDGE_MBAP, BRIDGE_EDGE_JSON };  // edgeReport.remoteFormat 0, 1, 2
/** Edge remote access on port n, ahead of the bridge's own paths: MBAP through a Modbus gateway's ADUs, RTU
 * and MBAP cut out of a TCP stream (frame_push), JSON one segment or datagram per request. */
void bridge_set_edge(int port, bridge_edge_fn fn, int framing);
