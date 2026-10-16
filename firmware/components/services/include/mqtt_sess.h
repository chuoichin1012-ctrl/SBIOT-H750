// The outbox and the subscriptions on one coreMQTT context (spec §8, Plan 09 ruling 3): what the `mqtt` task does
// after each connect and in its loop, host-tested with the real coreMQTT against a scripted broker
// (test_mqtt_session). The context is initialised once for the whole run (MQTT_Init resets the packet ids).
#pragma once
#include <stdbool.h>
#include "core_mqtt.h"
#include "mqtt_outbox.h"

typedef struct {
    MQTTContext_t *ctx;
    ob_t *ob;
    subs_t *subs;
    bool (*lock)(void);  // the outbox's and the list's mutex: false when not taken in time (nothing is done then)
    void (*unlock)(void);
    uint32_t (*now_ms)(void);
    uint32_t resent;  // PUBLISHes resent with DUP (diagnostics)
} mqtt_sess_t;

/** After MQTT_Connect: a new session sends everything in flight again as QUEUED (fresh ids); a kept one resends the
 * PUBLISHes waiting for PUBACK/PUBREC with DUP and their ids (coreMQTT resent the PUBRELs). Then every subscription. */
MQTTStatus_t ms_resume(mqtt_sess_t *s, bool kept);
/** QUEUED messages to the broker while fewer than OB_MAX_IN_FLIGHT are in flight, straight from their records; at most
 * 8 a call. Every function here gives MQTTIllegalState, having done nothing more, when the mutex is not taken. */
MQTTStatus_t ms_feed(mqtt_sess_t *s);
/** What this session has not subscribed yet (a listener added while connected, too). */
MQTTStatus_t ms_subscribe(mqtt_sess_t *s);
/** From coreMQTT's event callback: PUBACK, PUBREC or PUBCOMP for packet id. */
void ms_ack(mqtt_sess_t *s, uint8_t packet_type, uint16_t id);
