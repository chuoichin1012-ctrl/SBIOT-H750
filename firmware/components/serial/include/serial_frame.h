// The logic between a UART's DMA ring and its frames (spec §6.5, Plan 07a): line timing and BRR, the
// ring reader, transparent and Modbus RTU frame cuts with resync, line errors, XON/XOFF, and the TX
// controller. Pure C, host-tested (test_serial_frame); uart_drv.c and serial.c drive it on the board.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SF_FRAME_MAX 1460u  // the largest frame, as on the ESP32
#define SF_XON 0x11
#define SF_XOFF 0x13

// ---- line timing --------------------------------------------------------------------------------

typedef struct {
    uint32_t baud;      // 600 to 230 400
    uint8_t data_bits;  // 7 or 8
    uint8_t parity;     // 0 none, 1 odd, 2 even
    uint8_t stop_bits;  // 1 or 2
} sf_line_t;

/** Bits a character takes on the wire: start + data + parity + stop. */
unsigned sf_char_bits(const sf_line_t *l);
/** One character, in microseconds (rounded up). */
uint32_t sf_char_us(const sf_line_t *l);
/** Modbus t1.5 and t3.5 in microseconds; fixed at 750 and 1750 above 19 200 baud. */
uint32_t sf_t15_us(const sf_line_t *l);
uint32_t sf_t35_us(const sf_line_t *l);
/** The transparent frame gap in ms: packet_interval_ms, or 0 for 3.5 characters, at least 2 ms. */
uint32_t sf_gap_ms(const sf_line_t *l, uint32_t packet_interval_ms);
/** A silence of `us` as receiver-timeout bits (RTOR), rounded up, at least 1. */
uint32_t sf_rto_bits(const sf_line_t *l, uint32_t us);
/**
 * The USART prescaler and BRR for `baud` from kernel clock `clk` (Hz): the smallest prescaler of
 * {1,2,4,6,8,10,12,16,32,64,128,256} whose BRR (16x oversampling) fits 16..0xFFFF. presc_index is
 * the PRESC field (0..11). False when no prescaler fits. *err_ppm: the rate error.
 */
bool sf_brr(uint32_t clk, uint32_t baud, uint32_t *presc_index, uint32_t *brr, int32_t *err_ppm);

// ---- the DMA ring ---------------------------------------------------------------------------------

/**
 * The DMA's absolute write position: laps * size + (size - ndtr). A TC whose interrupt has not run
 * yet makes it look behind the reader: it is then one lap further.
 */
uint32_t sf_ring_wr(uint32_t laps, uint32_t ndtr, uint32_t size, uint32_t rd);
/** Bytes between reader rd and writer wr; more than `size` means the writer lapped the reader. */
static inline uint32_t sf_ring_pending(uint32_t rd, uint32_t wr) { return wr - rd; }

// ---- RX: frames -----------------------------------------------------------------------------------

typedef enum { SF_TRANSPARENT, SF_RTU } sf_mode_t;

typedef void (*sf_emit_fn)(void *ctx, const uint8_t *frame, size_t len);

typedef struct {
    sf_mode_t mode;
    uint32_t packet_len;  // transparent cut, 1..SF_FRAME_MAX
    bool xonxoff;         // flowControl 2: XON/XOFF are taken out of the data and pause our TX
    bool tx_paused;       // the device sent XOFF and no XON yet
    bool resync;          // RTU: after a violation, nothing is a frame until t3.5 of silence
    uint8_t frame[SF_FRAME_MAX];
    size_t len;
    sf_emit_fn emit;
    void *ctx;
    uint32_t overruns, line_errors, violations, dropped;  // dropped: frames lost to any of these
} sf_rx_t;

void sf_rx_init(sf_rx_t *r, sf_mode_t mode, uint32_t packet_len, bool xonxoff, sf_emit_fn emit, void *ctx);
/** Bytes read from the ring, in order. Transparent frames are cut at packet_len. */
void sf_rx_bytes(sf_rx_t *r, const uint8_t *p, size_t n);
/** The end of a frame: a transparent RTO, or an RTU one-shot that expired clean. Emits what was built. */
void sf_rx_end(sf_rx_t *r);
/** RTU: a byte came, or a character was still arriving, inside t1.5..t3.5. The frame is dropped. */
void sf_rx_violation(sf_rx_t *r);
/** A parity, framing, noise or USART overrun error: the frame being built is dropped. */
void sf_rx_line_error(sf_rx_t *r);
/** The DMA writer lapped the reader: the frame being built is dropped. */
void sf_rx_overrun(sf_rx_t *r);
/** Switches the cut (a transaction starts or ends); the frame being built is dropped. */
void sf_rx_mode(sf_rx_t *r, sf_mode_t mode);

/** RTU, at the one-shot's expiry: a violation when the write position moved since the RTO, or BUSY. */
static inline bool sf_rtu_violation(uint32_t wr_at_rto, uint32_t wr_now, bool busy) { return wr_now != wr_at_rto || busy; }

// ---- heartbeat payload -----------------------------------------------------------------------------

/**
 * The custom heartbeat content (serialHeartbeatContent 0): the text as it is, or with hex set, pairs of
 * hex digits (spaces ignored). Its length; 0 for an empty, odd or non-hex text.
 */
size_t sf_payload_custom(const char *custom, bool hex, uint8_t *out, size_t max);

// ---- TX -------------------------------------------------------------------------------------------

typedef enum {
    SF_TX_NONE,    // nothing to do now
    SF_TX_START,   // start a chunk of `n` bytes at `off` (copied into the TX buffer)
    SF_TX_PAUSE,   // clear DMAT: XOFF
    SF_TX_RESUME,  // set DMAT: XON
    SF_TX_ABORT,   // disable the stream, clear DMAT, drain the USART: the write failed
    SF_TX_DONE,    // the last bit has left (USART TC)
} sf_tx_action_t;

typedef struct {
    sf_tx_action_t action;
    size_t off, n;
} sf_tx_step_t;

typedef struct {
    size_t len, sent;   // bytes of the write, bytes whose chunk the DMA completed
    size_t chunk;       // the TX buffer's size
    bool busy;          // a chunk is with the DMA
    bool paused;        // XOFF in force
    bool draining;      // every chunk done: waiting for the USART's TC
    uint64_t deadline_us;
    int result;         // 0 while running or done, -1 on abort
} sf_tx_t;

/** A write of `len` bytes in chunks of at most `chunk`, failing at deadline_us. `paused`: XOFF in force. */
sf_tx_step_t sf_tx_begin(sf_tx_t *t, size_t len, size_t chunk, uint64_t deadline_us, bool paused);
sf_tx_step_t sf_tx_chunk_done(sf_tx_t *t);  // the stream's TC
sf_tx_step_t sf_tx_usart_tc(sf_tx_t *t);    // the USART's TC (only after the last chunk counts)
sf_tx_step_t sf_tx_xoff(sf_tx_t *t);
sf_tx_step_t sf_tx_xon(sf_tx_t *t);
sf_tx_step_t sf_tx_dma_error(sf_tx_t *t);
sf_tx_step_t sf_tx_tick(sf_tx_t *t, uint64_t now_us);  // the deadline
