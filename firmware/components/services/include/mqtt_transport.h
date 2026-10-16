// coreMQTT's transport on the gateway's connections (spec §8, Plan 09 ruling 2), host-tested (test_mqtt_transport).
// coreMQTT calls send/recv again with no pause while they return 0 (sendBuffer, recvExact), so "nothing yet" is
// answered only after a bounded wait for the socket, and two results in a row without progress also sleep a tick
// (select may return at once). Only reading skips the wait, when bytes are already inside mbedTLS.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "transport_interface.h"

#define MQTT_WAIT_MS 10u

typedef struct {
    int (*send)(void *c, const uint8_t *d, size_t n);  // >0, NET_AGAIN, NET_FAIL (conn_send_some)
    int (*recv)(void *c, uint8_t *b, size_t n);        // >0, 0 closed, NET_AGAIN, NET_FAIL (conn_recv)
    bool (*pending)(void *c);                          // bytes inside the TLS session (conn_pending)
    int (*wait)(void *c, bool write, uint32_t ms);     // lwip_select on the socket: >0 ready, 0 timeout, <0 error
    void (*tick)(void);                                // vTaskDelay(1)
    void *conn;
} mqtt_link_t;

struct NetworkContext {
    const mqtt_link_t *link;
    int idle;  // results in a row without progress
};

/** TransportSend_t: >0 bytes taken, 0 none yet (after a wait), -1 the connection is unusable. */
int32_t mqtt_tx(NetworkContext_t *nc, const void *buf, size_t n);
/** TransportRecv_t: >0 bytes, 0 none yet (after a wait, unless bytes are pending), -1 closed or failed. */
int32_t mqtt_rx(NetworkContext_t *nc, void *buf, size_t n);
