// 3GPP TS 27.010 basic mode, the initiator's side (spec §7.2, Plan 10): the frame codec, the opening of
// DLC0 then DLC1 (PPP) and DLC2 (AT) with T1/N2, the control channel (FCon/FCoff, MSC, Test, CLD, NSC),
// and the transmit queues. Pure C, no RTOS: the caller serialises the calls (one mutex), feeds the bytes
// received with cmux_input, calls cmux_tick for the timers, and sends what cmux_tx_next returns.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CMUX_N1 2048                   // AT+CMUX's N1: the largest information field
#define CMUX_FRAME_MAX (CMUX_N1 + 7)   // flag, address, control, 2 length octets, FCS, flag
#define CMUX_T1_MS 200
#define CMUX_N2 3
#define CMUX_DLCS 3                    // 0 control, 1 PPP, 2 AT
#define CMUX_CTRL_Q 2112u              // DLC0's queue: a Test response of N1 bytes fits
#define CMUX_DLC1_Q 4096u
#define CMUX_DLC2_Q 512u

// Control field values, P/F clear (cmux_frame sets P/F for SABM, DISC, UA and DM).
#define CMUX_SABM 0x2F
#define CMUX_UA 0x63
#define CMUX_DM 0x0F
#define CMUX_DISC 0x43
#define CMUX_UIH 0xEF

// Control channel message types with EA set; | 0x02 for a command, as is for a response.
#define CMUX_MSG_FCON 0xA1
#define CMUX_MSG_FCOFF 0x61
#define CMUX_MSG_MSC 0xE1
#define CMUX_MSG_TEST 0x21
#define CMUX_MSG_CLD 0xC1
#define CMUX_MSG_NSC 0x11

typedef enum { CMUX_CLOSED, CMUX_OPEN_CTRL, CMUX_OPEN_DLC, CMUX_RUNNING, CMUX_FAILED } cmux_state_t;
typedef enum { CMUX_EV_RUNNING, CMUX_EV_DLC_CLOSED, CMUX_EV_FAILED } cmux_event_t;

typedef struct {
    void (*data)(void *ctx, int dlc, const uint8_t *p, size_t n);  // a UIH payload on DLC1 or DLC2
    void (*event)(void *ctx, cmux_event_t ev, int dlc);
    void *ctx;
} cmux_io_t;

typedef struct {
    uint32_t bad_fcs, resyncs, too_long;  // received frames dropped
    uint32_t out_drop;                    // cmux_send refused on DLC1: no room (ppp_out_drop)
    uint32_t ctrl_drop;                   // a control frame that did not fit in DLC0's queue
    uint32_t nsc;                         // commands answered with NSC
} cmux_counters_t;

typedef struct {
    uint8_t *buf;
    size_t size, head, len;
} cmux_ring_t;

typedef struct cmux {
    cmux_io_t io;
    cmux_state_t state;
    // the parser
    uint8_t rx[CMUX_FRAME_MAX];
    int rx_state;
    size_t rx_len, rx_need, rx_hdr;
    uint8_t rx_addr, rx_ctrl, rx_len_oct[2];
    uint32_t last_rx_ms;
    // the opening
    bool open[CMUX_DLCS];
    uint8_t retries[CMUX_DLCS];
    uint32_t sent_ms[CMUX_DLCS];
    bool waiting[CMUX_DLCS];  // a SABM out, no UA yet
    // flow control
    bool fc_all_off;             // FCoff
    bool stopped[CMUX_DLCS];     // MSC FC per DLC
    // transmit
    uint8_t ctrl_mem[CMUX_CTRL_Q], dlc1_mem[CMUX_DLC1_Q], dlc2_mem[CMUX_DLC2_Q];
    size_t ctrl_len;             // DLC0: whole frames back to back, each after a 2-byte length
    cmux_ring_t q[CMUX_DLCS];    // q[1], q[2]: data bytes
    int rr;                      // the data DLC served last
    cmux_counters_t c;
} cmux_t;

/** One frame into out (at least CMUX_FRAME_MAX bytes for n == N1): its length, 0 when n > N1 or dlc > 63.
 *  `cr`: the C/R bit of the address octet (1 for the initiator's commands and data). */
size_t cmux_frame(uint8_t *out, int dlc, uint8_t control, bool cr, const uint8_t *info, size_t n);
/** 27.010 Annex B: the FCS over `n` octets. */
uint8_t cmux_fcs(const uint8_t *p, size_t n);

void cmux_init(cmux_t *m, const cmux_io_t *io);
void cmux_start(cmux_t *m, uint32_t now_ms);  // SABM on DLC0: OPEN_CTRL
void cmux_input(cmux_t *m, const uint8_t *p, size_t n, uint32_t now_ms);
void cmux_tick(cmux_t *m, uint32_t now_ms);   // T1 resends, N2
/** n or 0: all of it into the DLC's queue, or nothing (DLC1 counts out_drop). */
size_t cmux_send(cmux_t *m, int dlc, const uint8_t *p, size_t n);
/** The next whole frame to send, 0 for none: DLC0 first, then DLC1 and DLC2 in turn, when not stopped. */
size_t cmux_tx_next(cmux_t *m, uint8_t *out, size_t max);
void cmux_close(cmux_t *m);  // CLD on DLC0
cmux_state_t cmux_state(const cmux_t *m);
uint32_t cmux_last_rx_ms(const cmux_t *m);
const cmux_counters_t *cmux_counters(const cmux_t *m);
