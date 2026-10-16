// UART3 to the modem (spec §7.2, Plan 10): 115200 8N1, no RTS/CTS. RX by circular DMA into a 4 KB ring with
// HT, TC and IDLE notifying the reader task; TX by DMA from a bounce buffer, one writer (cmux_rx).
// DMA1 stream 6 (RX) and 7 (TX); both buffers in SRAM1 (non-cacheable, spec §5.4).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#define LTE_UART_RING 4096u
#define LTE_UART_TXBUF 2112u  // a CMUX frame of N1 bytes

/** Sets up UART3 and starts RX. 0 or -1. */
int lte_uart_init(void);
/** The task notified (task notification) on HT, TC and IDLE: set once it exists. */
void lte_uart_reader(TaskHandle_t reader);
const uint8_t *lte_uart_ring(void);
/** The DMA's absolute write position (laps * ring + offset). */
uint32_t lte_uart_wr(void);
/** Sends n bytes (n <= LTE_UART_TXBUF) and returns once the last bit has left: 0, or -1 on a timeout or a DMA
 *  error. Only the reader task writes. */
int lte_uart_write(const uint8_t *p, size_t n, uint32_t timeout_ms);
/** The TX bounce buffer itself: a frame built there is sent without a copy (lte_uart_write with p == it). */
uint8_t *lte_uart_txbuf(void);
uint32_t lte_uart_errors(void);  // USART line errors and DMA errors
