#include "serial_frame.h"
#include <string.h>

// ---- line timing --------------------------------------------------------------------------------

unsigned sf_char_bits(const sf_line_t *l)
{
    return 1u + l->data_bits + (l->parity ? 1u : 0u) + l->stop_bits;
}

static uint32_t div_up(uint64_t a, uint64_t b)
{
    return (uint32_t)((a + b - 1) / b);
}

uint32_t sf_char_us(const sf_line_t *l)
{
    return div_up((uint64_t)sf_char_bits(l) * 1000000u, l->baud);
}

// Modbus over Serial Line V1.02 §2.5.1.1: above 19 200 baud, fixed values.
uint32_t sf_t15_us(const sf_line_t *l)
{
    return l->baud > 19200 ? 750u : div_up((uint64_t)sf_char_bits(l) * 1500000u, l->baud);
}

uint32_t sf_t35_us(const sf_line_t *l)
{
    return l->baud > 19200 ? 1750u : div_up((uint64_t)sf_char_bits(l) * 3500000u, l->baud);
}

uint32_t sf_gap_ms(const sf_line_t *l, uint32_t packet_interval_ms)
{
    if (packet_interval_ms)
        return packet_interval_ms;
    uint32_t ms = div_up((uint64_t)sf_char_bits(l) * 3500u, l->baud);
    return ms < 2 ? 2 : ms;
}

uint32_t sf_rto_bits(const sf_line_t *l, uint32_t us)
{
    uint32_t b = div_up((uint64_t)us * l->baud, 1000000u);
    return b ? b : 1;
}

bool sf_brr(uint32_t clk, uint32_t baud, uint32_t *presc_index, uint32_t *brr, int32_t *err_ppm)
{
    static const uint16_t k_presc[] = {1, 2, 4, 6, 8, 10, 12, 16, 32, 64, 128, 256};  // RM0433 USART_PRESC
    if (!baud)
        return false;
    for (uint32_t i = 0; i < sizeof k_presc / sizeof k_presc[0]; i++) {
        uint32_t k = clk / k_presc[i];
        uint32_t b = (k + baud / 2) / baud;  // 16x oversampling: BRR = USARTDIV
        if (b >= 16 && b <= 0xFFFF) {
            *presc_index = i;
            *brr = b;
            int64_t real = (int64_t)k / b;
            *err_ppm = (int32_t)((real - (int64_t)baud) * 1000000 / (int64_t)baud);
            return true;
        }
    }
    return false;
}

// ---- the DMA ring ---------------------------------------------------------------------------------

uint32_t sf_ring_wr(uint32_t laps, uint32_t ndtr, uint32_t size, uint32_t rd)
{
    uint32_t wr = laps * size + (size - ndtr);
    // NDTR reloaded but the TC interrupt not yet run: the writer is a lap further than it looks.
    if ((int32_t)(wr - rd) < 0)
        wr += size;
    return wr;
}

// ---- RX ---------------------------------------------------------------------------------------------

void sf_rx_init(sf_rx_t *r, sf_mode_t mode, uint32_t packet_len, bool xonxoff, sf_emit_fn emit, void *ctx)
{
    memset(r, 0, sizeof *r);
    r->mode = mode;
    r->packet_len = packet_len && packet_len <= SF_FRAME_MAX ? packet_len : SF_FRAME_MAX;
    r->xonxoff = xonxoff;
    r->emit = emit;
    r->ctx = ctx;
}

static void drop(sf_rx_t *r)
{
    if (r->len)
        r->dropped++;
    r->len = 0;
}

void sf_rx_bytes(sf_rx_t *r, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t c = p[i];
        if (r->xonxoff && (c == SF_XOFF || c == SF_XON)) {
            r->tx_paused = c == SF_XOFF;
            continue;
        }
        if (r->resync)
            continue;  // RTU: nothing counts until t3.5 of silence
        if (r->len == SF_FRAME_MAX) {
            // An RTU frame can not be longer than this: drop it, and resync on the next silence.
            r->len = 0;
            r->dropped++;
            r->resync = r->mode == SF_RTU;
            continue;
        }
        r->frame[r->len++] = c;
        if (r->mode == SF_TRANSPARENT && r->len >= r->packet_len) {
            r->emit(r->ctx, r->frame, r->len);
            r->len = 0;
        }
    }
}

void sf_rx_end(sf_rx_t *r)
{
    if (r->resync) {  // this silence proves t3.5: the bus is in step again; the bytes since were dropped
        r->resync = false;
        r->len = 0;
        return;
    }
    if (r->len)
        r->emit(r->ctx, r->frame, r->len);
    r->len = 0;
}

void sf_rx_violation(sf_rx_t *r)
{
    r->violations++;
    drop(r);
    r->resync = true;
}

void sf_rx_line_error(sf_rx_t *r)
{
    r->line_errors++;
    drop(r);
}

void sf_rx_overrun(sf_rx_t *r)
{
    r->overruns++;
    drop(r);
}

void sf_rx_mode(sf_rx_t *r, sf_mode_t mode)
{
    drop(r);
    r->mode = mode;
    r->resync = false;
}

// ---- heartbeat payload -----------------------------------------------------------------------------

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = (char)(c | 0x20);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

size_t sf_payload_custom(const char *custom, bool hex, uint8_t *out, size_t max)
{
    size_t n = 0;
    if (!custom)
        return 0;
    if (!hex) {
        while (n < max && custom[n])
            n++;
        memcpy(out, custom, n);
        return n;
    }
    int hi = -1;
    for (const char *p = custom; *p && n < max; p++) {
        if (*p == ' ')
            continue;
        int v = hexval(*p);
        if (v < 0)
            return 0;
        if (hi < 0) {
            hi = v;
        } else {
            out[n++] = (uint8_t)(hi << 4 | v);
            hi = -1;
        }
    }
    return hi < 0 ? n : 0;
}

// ---- TX -------------------------------------------------------------------------------------------

static sf_tx_step_t step(sf_tx_action_t a, size_t off, size_t n)
{
    sf_tx_step_t s = {a, off, n};
    return s;
}

// The next chunk, when nothing is with the DMA and XON is in force.
static sf_tx_step_t next(sf_tx_t *t)
{
    if (t->busy || t->paused || t->draining)
        return step(SF_TX_NONE, 0, 0);
    size_t n = t->len - t->sent < t->chunk ? t->len - t->sent : t->chunk;
    t->busy = true;
    return step(SF_TX_START, t->sent, n);
}

static sf_tx_step_t abort_tx(sf_tx_t *t)
{
    t->result = -1;
    t->busy = false;
    t->draining = false;
    t->len = 0;
    return step(SF_TX_ABORT, 0, 0);
}

sf_tx_step_t sf_tx_begin(sf_tx_t *t, size_t len, size_t chunk, uint64_t deadline_us, bool paused)
{
    memset(t, 0, sizeof *t);
    t->len = len;
    t->chunk = chunk;
    t->deadline_us = deadline_us;
    t->paused = paused;
    if (!len) {
        t->draining = false;
        return step(SF_TX_DONE, 0, 0);
    }
    return next(t);
}

sf_tx_step_t sf_tx_chunk_done(sf_tx_t *t)
{
    if (!t->len || !t->busy)
        return step(SF_TX_NONE, 0, 0);
    size_t n = t->len - t->sent < t->chunk ? t->len - t->sent : t->chunk;
    t->sent += n;
    t->busy = false;
    if (t->sent == t->len) {
        t->draining = true;  // the last bit is still in the USART
        return step(SF_TX_NONE, 0, 0);
    }
    return next(t);
}

sf_tx_step_t sf_tx_usart_tc(sf_tx_t *t)
{
    if (!t->len || !t->draining)
        return step(SF_TX_NONE, 0, 0);
    t->draining = false;
    t->len = 0;  // idle again
    return step(SF_TX_DONE, 0, 0);
}

sf_tx_step_t sf_tx_xoff(sf_tx_t *t)
{
    bool was = t->paused;
    t->paused = true;
    return t->len && t->busy && !was ? step(SF_TX_PAUSE, 0, 0) : step(SF_TX_NONE, 0, 0);
}

sf_tx_step_t sf_tx_xon(sf_tx_t *t)
{
    bool was = t->paused;
    t->paused = false;
    if (!t->len || !was)
        return step(SF_TX_NONE, 0, 0);
    if (t->busy)
        return step(SF_TX_RESUME, 0, 0);
    return next(t);
}

sf_tx_step_t sf_tx_dma_error(sf_tx_t *t)
{
    return t->len ? abort_tx(t) : step(SF_TX_NONE, 0, 0);
}

sf_tx_step_t sf_tx_tick(sf_tx_t *t, uint64_t now_us)
{
    if (t->len && now_us >= t->deadline_us)
        return abort_tx(t);
    return step(SF_TX_NONE, 0, 0);
}
