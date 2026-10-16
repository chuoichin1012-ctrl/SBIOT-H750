// The MQTT gateway (spec §8, Plan 09); see mqttgw.c.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

/** Reads mqtt, mqttPublish, mqttSubscribe once (and system's bufferOffline) and starts the `mqtt` and `mqtt_io`
 * tasks when mqtt.enabled. From services_start, after the bridges and before edge_start (Edge adds listeners). */
void mqttgw_start(const cJSON *sys);
/** /api/status mqtt.state: NOT RUNNING, CONNECTING, CONNECTED, REFUSED, NO CA CERTIFICATE, NO CLIENT CERTIFICATE,
 * NO MEMORY. */
/** MQTT was on at boot (its config then): what /api/status reports until the restart (Plan 10c, Codex Minor 12). */
bool mqttgw_enabled(void);
void mqttgw_status(char *state, size_t len);
/** A message into the outbox (Edge's reports and replies); false when MQTT does not run, the broker is away without
 * bufferOffline, or the outbox has no room. Never waits on the network. */
bool mqttgw_publish(const char *topic, const void *data, size_t len, int qos, bool retain);
/** Broker messages on `topic` (wildcards allowed) go to fn, in `mqtt_io` (fn may wait on a device). Subscribed at
 * once when connected, and again after each connect. False when MQTT does not run or the table is full. */
typedef void (*mqttgw_rx_fn)(const uint8_t *data, size_t len);
bool mqttgw_add_listener(const char *topic, int qos, mqttgw_rx_fn fn);
