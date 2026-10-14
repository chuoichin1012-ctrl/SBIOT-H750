// MQTT helpers for the gateway (pure C, host-tested).
#pragma once

#include <stdbool.h>
#include <stddef.h>

/** True when `topic` (len bytes, not NUL-terminated) matches subscription `filter` (+ and # wildcards). */
bool mqtt_topic_match(const char *filter, const char *topic, size_t len);

/**
 * Topic distribution: `data` starts with `alias` followed by a comma. Returns
 * the payload offset (after the comma), or 0 when the alias does not match.
 */
size_t mqtt_alias_match(const char *alias, const char *data, size_t len);

/**
 * Custom mode: serial data names its own topic as "topic,qos,retain,payload"
 * (qos 0-2, retain 0/1/ON/OFF; the payload may contain commas). On success fills the
 * fields (`topic` NUL-terminated, payload points into `data`) and returns true.
 */
bool mqtt_custom_parse(const char *data, size_t len, char *topic, size_t topic_max, int *qos, bool *retain,
                       const char **payload, size_t *payload_len);
