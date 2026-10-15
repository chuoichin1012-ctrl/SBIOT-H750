// USART1 and USART2 on the board (spec §6.5, Plan 07a): RX by circular DMA into a ring, the receiver
// timeout, and our own LL interrupt handlers (HAL's would abort RX on RTOF); TX by DMA from a buffer of
// our own; the chip's RS485 driver-enable (DEM); TIM2 CC1/CC2 as the Modbus t3.5 one-shots.
// The events go to a queue that serial.c's serial_rx[n] task drains.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "serial_frame.h"

#define UART_PORTS 2
#define UART_RING 2048u   // RX ring per port (spec §6.1: 2 KB, about 90 ms at 230 400)
#define UART_TXBUF 2048u  // TX buffer per port (spec §5.4)

typedef enum {
    UART_EV_DATA,      // HT or TC: bytes up to `wr` are in the ring
    UART_EV_GAP,       // transparent: the receiver timeout; the frame ends at `wr`
    UART_EV_RTU_END,   // RTU: t3.5 of silence after `wr`; the frame ends there
    UART_EV_RTU_VIOL,  // RTU: a byte, or a character still arriving, inside t1.5..t3.5
    UART_EV_ERROR,     // a parity, framing, noise or overrun error; bytes up to `wr` are suspect
    UART_EV_MODE,      // serial.c: the cut changes (`arg`: sf_mode_t)
    UART_EV_LOST,      // the queue was full: events were lost
} uart_ev_type_t;

typedef struct {
    uint8_t type;   // uart_ev_type_t
    uint8_t arg;
    uint32_t wr;    // the DMA's absolute write position when the event happened
} uart_ev_t;

/**
 * Sets up port 1 (USART1) or 2 (USART2) at `line` and starts RX. `gap_us`: the transparent frame gap
 * (receiver timeout). Events go to `q` (uart_ev_t). 0, or -1 (the line can not be set up).
 */
int uart_drv_init(int port, const sf_line_t *line, uint32_t gap_us, bool xonxoff, QueueHandle_t q);
/** The ring, its size, and the DMA's absolute write position now. */
const uint8_t *uart_drv_ring(int port);
uint32_t uart_drv_wr(int port);
/** A character is arriving now (the USART's BUSY). */
bool uart_drv_rx_busy(int port);
/** RTU (t1.5 receiver timeout, t3.5 one-shot) or transparent (the gap), from the next byte on. */
void uart_drv_set_mode(int port, sf_mode_t mode);
/** The device's XOFF (true) or XON, seen by serial_rx. */
void uart_drv_flow(int port, bool paused);
/**
 * Sends len bytes (from anywhere) and returns once the last bit has left: 0, or -1 when the deadline
 * passed (XOFF without XON, no TC) or the DMA failed. The caller holds the port's bus mutex.
 */
int uart_drv_write(int port, const uint8_t *data, size_t len, uint32_t timeout_ms);
/** Bench: the next write sees an XOFF 20 ms in and an XON 1 s later (fault injection, Plan 07a). */
void uart_drv_inject_xoff(int port);
/** Counters: USART errors seen, events lost. */
void uart_drv_counters(int port, uint32_t *errors, uint32_t *lost);
