// The serial ports UART1 and UART2 and their bus arbitration (spec §6.4, §6.5), the ESP32's serial.c made
// one instance per port (`port` 1 or 2 in every call):
//  - the bridge, MQTT and the WebSocket console get whole frames through serial_subscribe() and send
//    with serial_write();
//  - Modbus users (Edge, the Modbus gateway, polling) use serial_transact(), which holds the bus for one
//    request and its answer, cut by Modbus RTU's t1.5/t3.5.
// One mutex per port, the "bus", serialises writes, transactions and nothing else; only serial_rx[n]
// reads. While a transaction is in flight, what arrives is its answer, unless it plainly comes from
// another slave; after serial_write() the device's answer belongs to the subscribers for the reply window.
// Settings come from port<n> and system at boot (spec §9.1).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

#define SERIAL_PORTS 2
#define SERIAL_FRAME_MAX 1460

/** Starts port 1..2 with its config `pcfg` (port<n>) and `sys` (system). 0, or -1 (logged). */
int serial_start(int port, const cJSON *pcfg, const cJSON *sys);

/** Called with each frame, from serial_rx[n] (copy what you keep; do not block). */
typedef void (*serial_rx_fn)(int port, const uint8_t *data, size_t len, void *ctx);
int serial_subscribe(int port, serial_rx_fn fn, void *ctx);

/** Every frame of the port, both ways (the WebSocket console's monitor, the user 2026-10-17): each frame sent, from
 * its writer's task with the bus held (bridge, MQTT, Edge, IO, the console), and each frame received, from
 * serial_rx[n] (also those a transaction takes). fn must not block. One monitor per port; 0, or -1. */
typedef void (*serial_mon_fn)(int port, bool tx, const uint8_t *data, size_t len, void *ctx);
int serial_monitor(int port, serial_mon_fn fn, void *ctx);

/**
 * Sends bytes: waits for a transaction in flight and, with 485 collision avoidance, an idle bus (bounded:
 * the idle time plus `timeout_ms`, then nothing is sent). 0, SERIAL_BUSY when the bus mutex was not
 * obtained within `timeout_ms`, or -1 when the bytes did not go out.
 */
int serial_write(int port, const uint8_t *data, size_t len, uint32_t timeout_ms);

#define SERIAL_DROPPED (-2)  // serial_transact: an answer came, but broke t1.5 (Modbus §2.5.1.1)
#define SERIAL_BUSY (-3)     // serial_write: the bus stayed taken (a transaction, or a lock that is stuck)
/**
 * Exclusive request and answer: sends `req`, then collects one RTU frame. `unit` (0-255, or -1): the
 * first byte the answer must start with; a frame from another slave goes to the subscribers. The
 * answer's length, 0 on timeout, SERIAL_DROPPED, or -1 on error.
 */
int serial_transact(int port, const uint8_t *req, size_t len, uint8_t *resp, size_t max, int unit, uint32_t timeout_ms);

typedef struct {
    uint32_t tx, rx;                                   // bytes
    uint32_t overruns, line_errors, violations, dropped, lost;  // see serial_frame.h, uart_drv.h
} serial_counters_t;
void serial_counters(int port, serial_counters_t *c);
/** The time (now_us) of the last byte either way. */
uint64_t serial_last_activity_us(int port);
