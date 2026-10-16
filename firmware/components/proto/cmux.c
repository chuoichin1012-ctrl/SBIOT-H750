// 3GPP TS 27.010 basic mode, the initiator's side (spec §7.2, Plan 10). See cmux.h.
#include "cmux.h"
#include <string.h>

enum { P_SYNC, P_ADDR, P_CTRL, P_LEN1, P_LEN2, P_INFO, P_FCS, P_END };

// ---- the codec --------------------------------------------------------------------------------------

uint8_t cmux_fcs(const uint8_t *p, size_t n)
{
    uint8_t crc = 0xFF;  // Annex B: reflected x^8 + x^2 + x + 1
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = crc & 1 ? (uint8_t)(crc >> 1 ^ 0xE0) : (uint8_t)(crc >> 1);
    }
    return (uint8_t)(0xFF - crc);
}

static uint8_t with_pf(uint8_t control)
{
    uint8_t c = control & (uint8_t)~0x10;
    return c == CMUX_SABM || c == CMUX_DISC || c == CMUX_UA || c == CMUX_DM ? (uint8_t)(c | 0x10) : control;
}

/** The header of a frame with n information bytes into out; its length (4 or 5). */
static size_t header(uint8_t *out, int dlc, uint8_t control, bool cr, size_t n)
{
    out[0] = 0xF9;
    out[1] = (uint8_t)(dlc << 2 | (cr ? 2 : 0) | 1);
    out[2] = with_pf(control);
    if (n < 128) {
        out[3] = (uint8_t)(n << 1 | 1);
        return 4;
    }
    out[3] = (uint8_t)((n & 0x7F) << 1);  // EA clear: a second octet follows
    out[4] = (uint8_t)(n >> 7);           // L8..L15, no EA bit (27.010 §5.2.1.5)
    return 5;
}

/** Closes a frame whose header (hdr bytes) and n information bytes are in out: the FCS and the flag. */
static size_t trailer(uint8_t *out, size_t hdr, size_t n)
{
    out[hdr + n] = cmux_fcs(out + 1, hdr - 1);  // UIH: the information field is not covered (§5.2.1.6)
    out[hdr + n + 1] = 0xF9;
    return hdr + n + 2;
}

/** A frame whose information is a then b. */
static size_t put_frame(uint8_t *out, int dlc, uint8_t control, bool cr, const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
    size_t n = an + bn;
    if (n > CMUX_N1 || dlc < 0 || dlc > 63)
        return 0;
    size_t hdr = header(out, dlc, control, cr, n);
    if (an)
        memcpy(out + hdr, a, an);
    if (bn)
        memcpy(out + hdr + an, b, bn);
    return trailer(out, hdr, n);
}

size_t cmux_frame(uint8_t *out, int dlc, uint8_t control, bool cr, const uint8_t *info, size_t n)
{
    return put_frame(out, dlc, control, cr, info, n, NULL, 0);
}

// ---- the queues ---------------------------------------------------------------------------------------

static size_t ring_push(cmux_ring_t *r, const uint8_t *p, size_t n)
{
    if (r->size - r->len < n)
        return 0;
    size_t tail = (r->head + r->len) % r->size, first = r->size - tail < n ? r->size - tail : n;
    memcpy(r->buf + tail, p, first);
    memcpy(r->buf, p + first, n - first);
    r->len += n;
    return n;
}

static void ring_pop(cmux_ring_t *r, uint8_t *out, size_t n)
{
    size_t first = r->size - r->head < n ? r->size - r->head : n;
    memcpy(out, r->buf + r->head, first);
    memcpy(out + first, r->buf, n - first);
    r->head = (r->head + n) % r->size;
    r->len -= n;
}

/** A control frame (DLC0, or a UA/DM/SABM on any DLC) at the end of DLC0's queue. */
static void ctrl_frame(cmux_t *m, int dlc, uint8_t control, bool cr, const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
    size_t need = 2 + an + bn + 7;
    if (an + bn > CMUX_N1 || CMUX_CTRL_Q - m->ctrl_len < need) {
        m->c.ctrl_drop++;
        return;
    }
    uint8_t *at = m->ctrl_mem + m->ctrl_len;
    size_t len = put_frame(at + 2, dlc, control, cr, a, an, b, bn);
    at[0] = (uint8_t)(len >> 8), at[1] = (uint8_t)len;
    m->ctrl_len += 2 + len;
}

/** A control channel message: its type, its EA-coded length, its value. */
static void ctrl_msg(cmux_t *m, uint8_t type, const uint8_t *val, size_t n)
{
    uint8_t h[3];
    size_t hn = 0;
    h[hn++] = type;
    if (n < 128) {
        h[hn++] = (uint8_t)(n << 1 | 1);
    } else {
        h[hn++] = (uint8_t)((n & 0x7F) << 1);
        h[hn++] = (uint8_t)(n >> 7 << 1 | 1);
    }
    ctrl_frame(m, 0, CMUX_UIH, true, h, hn, val, n);
}

static void sabm(cmux_t *m, int dlc, uint32_t now_ms)
{
    ctrl_frame(m, dlc, CMUX_SABM, true, NULL, 0, NULL, 0);
    m->waiting[dlc] = true;
    m->sent_ms[dlc] = now_ms;
}

static void event(cmux_t *m, cmux_event_t ev, int dlc)
{
    if (m->io.event)
        m->io.event(m->io.ctx, ev, dlc);
}

// ---- receiving ----------------------------------------------------------------------------------------

/** One control channel message (a command from the modem gets its response; responses are taken as read). */
static void control(cmux_t *m, const uint8_t *p, size_t n)
{
    while (n >= 2) {
        uint8_t type = p[0];
        size_t len = p[1] >> 1, o = 2;
        if (!(p[1] & 1)) {
            if (n < 3)
                return;
            len |= (size_t)(p[2] >> 1) << 7;
            o = 3;
        }
        if (o + len > n)
            return;
        const uint8_t *val = p + o;
        if (type & 0x02) {  // a command
            uint8_t resp = type & (uint8_t)~0x02;
            switch (resp) {
            case CMUX_MSG_FCON:
                m->fc_all_off = false;
                ctrl_msg(m, resp, NULL, 0);
                break;
            case CMUX_MSG_FCOFF:
                m->fc_all_off = true;  // every data DLC; DLC0 goes on (§5.4.6.3.5)
                ctrl_msg(m, resp, NULL, 0);
                break;
            case CMUX_MSG_MSC:
                if (len >= 2 && (val[0] >> 2) < CMUX_DLCS)
                    m->stopped[val[0] >> 2] = (val[1] & 0x02) != 0;  // FC: that DLC alone (§5.4.6.3.7)
                ctrl_msg(m, resp, val, len);
                break;
            case CMUX_MSG_TEST:
                ctrl_msg(m, resp, val, len);  // the whole verification pattern back (§5.4.6.3.4)
                break;
            case CMUX_MSG_CLD:
                ctrl_msg(m, resp, NULL, 0);
                m->state = CMUX_CLOSED;
                event(m, CMUX_EV_DLC_CLOSED, 0);
                break;
            default:
                m->c.nsc++;
                ctrl_msg(m, CMUX_MSG_NSC, &type, 1);
                break;
            }
        }
        p += o + len;
        n -= o + len;
    }
}

static void frame(cmux_t *m, uint32_t now_ms)
{
    int dlc = m->rx_addr >> 2;
    uint8_t c = m->rx_ctrl & (uint8_t)~0x10;
    m->last_rx_ms = now_ms;
    if (dlc >= CMUX_DLCS)
        return;
    if (c == CMUX_UA && m->waiting[dlc]) {
        m->waiting[dlc] = false;
        m->open[dlc] = true;
        if (dlc == 0 && m->state == CMUX_OPEN_CTRL) {
            sabm(m, 1, now_ms);
            sabm(m, 2, now_ms);
            m->state = CMUX_OPEN_DLC;
        } else if (dlc > 0 && m->state == CMUX_OPEN_DLC) {
            uint8_t v[2] = {(uint8_t)(dlc << 2 | 3), 0x8D};  // RTC, RTR, DV; FC clear; before any data (§5.4.6.3.7)
            ctrl_msg(m, CMUX_MSG_MSC | 2, v, 2);
            if (m->open[1] && m->open[2]) {
                m->state = CMUX_RUNNING;
                event(m, CMUX_EV_RUNNING, 0);
            }
        }
    } else if (c == CMUX_DM) {
        if (m->waiting[dlc]) {
            m->waiting[dlc] = false;
            m->state = CMUX_FAILED;
            event(m, CMUX_EV_FAILED, dlc);
        } else if (m->open[dlc]) {
            m->open[dlc] = false;
            event(m, CMUX_EV_DLC_CLOSED, dlc);
        }
    } else if (c == CMUX_DISC) {
        ctrl_frame(m, dlc, CMUX_UA, false, NULL, 0, NULL, 0);
        if (m->open[dlc]) {
            m->open[dlc] = false;
            event(m, CMUX_EV_DLC_CLOSED, dlc);
        }
    } else if (c == CMUX_SABM) {
        ctrl_frame(m, dlc, CMUX_DM, false, NULL, 0, NULL, 0);  // the gateway opens the DLCs, never the modem
    } else if (c == CMUX_UIH || c == 0x03) {
        if (dlc == 0)
            control(m, m->rx, m->rx_len);
        else if (m->io.data && m->rx_len)
            m->io.data(m->io.ctx, dlc, m->rx, m->rx_len);
    }
}

void cmux_input(cmux_t *m, const uint8_t *p, size_t n, uint32_t now_ms)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t b = p[i];
        switch (m->rx_state) {
        case P_SYNC:
            if (b == 0xF9)
                m->rx_state = P_ADDR;
            break;
        case P_ADDR:
            if (b == 0xF9)
                break;  // flags between frames
            if (!(b & 1)) {
                m->c.resyncs++;
                m->rx_state = P_SYNC;
                break;
            }
            m->rx_addr = b;
            m->rx_state = P_CTRL;
            break;
        case P_CTRL:
            m->rx_ctrl = b;
            m->rx_state = P_LEN1;
            break;
        case P_LEN1:
        case P_LEN2:
            m->rx_len_oct[m->rx_state - P_LEN1] = b;
            if (m->rx_state == P_LEN1 && !(b & 1)) {
                m->rx_state = P_LEN2;
                break;
            }
            m->rx_hdr = m->rx_state == P_LEN1 ? 3 : 4;
            // the second octet is L8..L15, no EA bit (27.010 §5.2.1.5)
            m->rx_need = m->rx_state == P_LEN1 ? (size_t)(b >> 1) : (size_t)(m->rx_len_oct[0] >> 1) | (size_t)b << 7;
            m->rx_len = 0;
            if (m->rx_need > CMUX_N1) {
                m->c.too_long++;
                m->rx_state = P_SYNC;
            } else {
                m->rx_state = m->rx_need ? P_INFO : P_FCS;
            }
            break;
        case P_INFO:
            m->rx[m->rx_len++] = b;
            if (m->rx_len == m->rx_need)
                m->rx_state = P_FCS;
            break;
        case P_FCS: {
            uint8_t h[4] = {m->rx_addr, m->rx_ctrl, m->rx_len_oct[0], m->rx_len_oct[1]};
            if (cmux_fcs(h, m->rx_hdr) != b) {
                m->c.bad_fcs++;
                memcpy(m->c.last_bad, h, 4);
                m->c.last_bad[4] = b, m->c.last_bad[5] = cmux_fcs(h, m->rx_hdr);
                m->rx_state = P_SYNC;
            } else {
                m->rx_state = P_END;
            }
            break;
        }
        case P_END:
            if (b == 0xF9) {
                frame(m, now_ms);
                m->rx_state = P_ADDR;  // a closing flag may open the next frame
            } else {
                m->c.resyncs++;
                m->rx_state = P_SYNC;
            }
            break;
        }
    }
}

// ---- the life cycle -------------------------------------------------------------------------------------

void cmux_init(cmux_t *m, const cmux_io_t *io)
{
    memset(m, 0, sizeof *m);
    m->io = *io;
    m->q[1] = (cmux_ring_t){.buf = m->dlc1_mem, .size = CMUX_DLC1_Q};
    m->q[2] = (cmux_ring_t){.buf = m->dlc2_mem, .size = CMUX_DLC2_Q};
    m->rr = 2;  // DLC1 is served first
}

void cmux_start(cmux_t *m, uint32_t now_ms)
{
    sabm(m, 0, now_ms);
    m->state = CMUX_OPEN_CTRL;
}

void cmux_tick(cmux_t *m, uint32_t now_ms)
{
    if (m->state != CMUX_OPEN_CTRL && m->state != CMUX_OPEN_DLC)
        return;
    for (int dlc = 0; dlc < CMUX_DLCS; dlc++) {
        if (!m->waiting[dlc] || now_ms - m->sent_ms[dlc] < CMUX_T1_MS)
            continue;
        if (m->retries[dlc] >= CMUX_N2) {
            m->waiting[dlc] = false;
            m->state = CMUX_FAILED;
            event(m, CMUX_EV_FAILED, dlc);
            return;
        }
        m->retries[dlc]++;
        sabm(m, dlc, now_ms);
    }
}

size_t cmux_send(cmux_t *m, int dlc, const uint8_t *p, size_t n)
{
    if (dlc < 1 || dlc >= CMUX_DLCS)
        return 0;
    size_t took = ring_push(&m->q[dlc], p, n);
    if (!took && n && dlc == 1)
        m->c.out_drop++;
    return took;
}

size_t cmux_room(const cmux_t *m, int dlc)
{
    return dlc >= 1 && dlc < CMUX_DLCS ? m->q[dlc].size - m->q[dlc].len : 0;
}

size_t cmux_tx_next(cmux_t *m, uint8_t *out, size_t max)
{
    if (m->ctrl_len) {
        size_t len = (size_t)m->ctrl_mem[0] << 8 | m->ctrl_mem[1];
        if (len > max)
            return 0;
        memcpy(out, m->ctrl_mem + 2, len);
        memmove(m->ctrl_mem, m->ctrl_mem + 2 + len, m->ctrl_len - 2 - len);
        m->ctrl_len -= 2 + len;
        return len;
    }
    if (m->state != CMUX_RUNNING || m->fc_all_off || max <= 7)
        return 0;
    for (int k = 1; k < CMUX_DLCS; k++) {
        int dlc = (m->rr + k - 1) % (CMUX_DLCS - 1) + 1;  // 1, 2 in turn after the last one served
        cmux_ring_t *r = &m->q[dlc];
        if (!m->open[dlc] || m->stopped[dlc] || !r->len)
            continue;
        size_t n = r->len;
        if (n > CMUX_N1)
            n = CMUX_N1;
        if (n > max - 7)
            n = max - 7;
        size_t hdr = header(out, dlc, CMUX_UIH, true, n);
        ring_pop(r, out + hdr, n);
        m->rr = dlc;
        return trailer(out, hdr, n);
    }
    return 0;
}

void cmux_close(cmux_t *m)
{
    ctrl_msg(m, CMUX_MSG_CLD | 2, NULL, 0);
    m->state = CMUX_CLOSED;
}

cmux_state_t cmux_state(const cmux_t *m) { return m->state; }
uint32_t cmux_last_rx_ms(const cmux_t *m) { return m->last_rx_ms; }
const cmux_counters_t *cmux_counters(const cmux_t *m) { return &m->c; }
