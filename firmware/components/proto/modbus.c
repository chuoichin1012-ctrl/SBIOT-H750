#include "modbus.h"

#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

uint16_t mb_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

bool mb_rtu_valid(const uint8_t *frame, size_t len)
{
    if (len < 4) return false;
    uint16_t crc = mb_crc16(frame, len - 2);
    return frame[len - 2] == (crc & 0xFF) && frame[len - 1] == (crc >> 8);
}

size_t mb_rtu_seal(uint8_t *frame, size_t len)
{
    uint16_t crc = mb_crc16(frame, len);
    frame[len] = (uint8_t)crc;
    frame[len + 1] = (uint8_t)(crc >> 8);
    return len + 2;
}

size_t mb_rtu_read(uint8_t *out, uint8_t unit, uint8_t fc, uint16_t start, uint16_t count)
{
    out[0] = unit;
    out[1] = fc;
    put16(out + 2, start);
    put16(out + 4, count);
    return mb_rtu_seal(out, 6);
}

size_t mb_rtu_write1(uint8_t *out, uint8_t unit, uint8_t fc, uint16_t addr, uint16_t value)
{
    out[0] = unit;
    out[1] = fc;
    put16(out + 2, addr);
    put16(out + 4, fc == 5 ? (value ? 0xFF00 : 0x0000) : value);
    return mb_rtu_seal(out, 6);
}

size_t mb_rtu_write_regs(uint8_t *out, uint8_t unit, uint16_t start, const uint16_t *regs, uint16_t count)
{
    if (count == 0 || count > 123) return 0;
    out[0] = unit;
    out[1] = 16;
    put16(out + 2, start);
    put16(out + 4, count);
    out[6] = (uint8_t)(count * 2);
    for (uint16_t i = 0; i < count; i++) put16(out + 7 + 2 * i, regs[i]);
    return mb_rtu_seal(out, 7 + 2 * count);
}

size_t mb_rtu_reply_len(const uint8_t *req, size_t len)
{
    if (len < 8) return 0;
    uint16_t count = be16(req + 4);
    switch (req[1]) {
    case 1:
    case 2: return 5 + (count + 7) / 8;
    case 3:
    case 4: return 5 + 2 * (size_t)count;
    case 5:
    case 6:
    case 15:
    case 16: return 8;
    default: return 0;
    }
}

int mb_tcp_frame_len(const uint8_t *buf, size_t len)
{
    if (len < 7) return 0;
    if (be16(buf + 2) != 0) return -1;  // protocol id is always 0
    size_t n = be16(buf + 4);
    if (n < 2 || n > 254) return -1;
    return len >= 6 + n ? (int)(6 + n) : 0;
}

size_t mb_tcp_to_rtu(const uint8_t *adu, size_t len, uint8_t *rtu, size_t max, uint16_t *tid)
{
    int n = mb_tcp_frame_len(adu, len);
    if (n <= 0) return 0;
    size_t body = (size_t)n - 6;  // unit + PDU
    if (body + 2 > max) return 0;
    *tid = be16(adu);
    memcpy(rtu, adu + 6, body);
    return mb_rtu_seal(rtu, body);
}

size_t mb_tcp_wrap(const uint8_t *unit_pdu, size_t len, uint16_t tid, uint8_t *adu, size_t max)
{
    if (len < 2 || len + 6 > max) return 0;
    put16(adu, tid);
    put16(adu + 2, 0);
    put16(adu + 4, (uint16_t)len);
    memcpy(adu + 6, unit_pdu, len);
    return len + 6;
}

size_t mb_rtu_to_tcp(const uint8_t *rtu, size_t len, uint16_t tid, uint8_t *adu, size_t max)
{
    if (!mb_rtu_valid(rtu, len)) return 0;
    return mb_tcp_wrap(rtu, len - 2, tid, adu, max);
}

size_t mb_rtu_request_len(const uint8_t *buf, size_t len)
{
    if (len < 2) return 8;  // not enough to tell yet: at least a short request
    switch (buf[1]) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6: return 8;
    case 15:
    case 16: return len < 7 ? 9 : 9 + (size_t)buf[6];
    default: return 0;
    }
}

static size_t exception(const uint8_t *req, uint8_t code, uint8_t *out)
{
    out[0] = req[0];
    out[1] = req[1] | 0x80;
    out[2] = code;
    return 3;
}

size_t mb_serve(const mb_map_t *m, const uint8_t *req, size_t len, uint8_t *out, size_t max)
{
    if (len < 2 || max < 3) return 0;
    uint8_t fc = req[1];
    if (len < 6) return exception(req, MB_EX_ILLEGAL_VALUE, out);
    uint16_t addr = be16(req + 2), count = be16(req + 4);
    out[0] = req[0];
    out[1] = fc;
    switch (fc) {
    case 1:
    case 2: {
        if (!m->read_bit) return exception(req, MB_EX_ILLEGAL_FUNCTION, out);
        if (count < 1 || count > 2000 || 3 + ((size_t)count + 7) / 8 > max) return exception(req, MB_EX_ILLEGAL_VALUE, out);
        size_t bytes = ((size_t)count + 7) / 8;
        memset(out + 3, 0, bytes);
        for (uint16_t i = 0; i < count; i++) {
            bool on = false;
            uint8_t ex = m->read_bit(m->ctx, fc, (uint16_t)(addr + i), &on);
            if (ex) return exception(req, ex, out);
            if (on) out[3 + i / 8] |= (uint8_t)(1 << (i % 8));
        }
        out[2] = (uint8_t)bytes;
        return 3 + bytes;
    }
    case 3:
    case 4: {
        if (!m->read_reg) return exception(req, MB_EX_ILLEGAL_FUNCTION, out);
        if (count < 1 || count > 125 || 3 + 2 * (size_t)count > max) return exception(req, MB_EX_ILLEGAL_VALUE, out);
        for (uint16_t i = 0; i < count; i++) {
            uint16_t v = 0;
            uint8_t ex = m->read_reg(m->ctx, fc, (uint16_t)(addr + i), &v);
            if (ex) return exception(req, ex, out);
            put16(out + 3 + 2 * i, v);
        }
        out[2] = (uint8_t)(2 * count);
        return 3 + 2 * (size_t)count;
    }
    case 5:
        if (!m->write_bit) return exception(req, MB_EX_ILLEGAL_FUNCTION, out);
        if (count != 0xFF00 && count != 0x0000) return exception(req, MB_EX_ILLEGAL_VALUE, out);
        {
            uint8_t ex = m->write_bit(m->ctx, addr, count == 0xFF00);
            if (ex) return exception(req, ex, out);
        }
        memcpy(out, req, 6);  // echo
        return 6;
    case 6:
        if (!m->write_reg) return exception(req, MB_EX_ILLEGAL_FUNCTION, out);
        {
            uint8_t ex = m->write_reg(m->ctx, addr, count);
            if (ex) return exception(req, ex, out);
        }
        memcpy(out, req, 6);
        return 6;
    case 15:
    case 16: {
        bool regs = fc == 16;
        if (regs ? !m->write_reg && !m->write_regs : !m->write_bit) return exception(req, MB_EX_ILLEGAL_FUNCTION, out);
        size_t bytes = regs ? 2 * (size_t)count : ((size_t)count + 7) / 8;
        if (len < 7 || count < 1 || count > (regs ? 123 : 1968) || req[6] != bytes || len < 7 + bytes)
            return exception(req, MB_EX_ILLEGAL_VALUE, out);
        if (regs && m->write_regs) {  // the block callback validates the whole range itself
            uint16_t values[123];
            for (uint16_t i = 0; i < count; i++) values[i] = be16(req + 7 + 2 * i);
            uint8_t ex = m->write_regs(m->ctx, addr, values, count);
            if (ex) return exception(req, ex, out);
            memcpy(out, req, 6);
            return 6;
        }
        // Check the whole range first: a bad address in the middle must change nothing.
        for (uint16_t i = 0; m->exists && i < count; i++) {
            if (!m->exists(m->ctx, regs ? 3 : 1, (uint16_t)(addr + i))) return exception(req, MB_EX_ILLEGAL_ADDRESS, out);
        }
        for (uint16_t i = 0; i < count; i++) {
            uint8_t ex = regs ? m->write_reg(m->ctx, (uint16_t)(addr + i), be16(req + 7 + 2 * i))
                              : m->write_bit(m->ctx, (uint16_t)(addr + i), (req[7 + i / 8] >> (i % 8)) & 1);
            if (ex) return exception(req, ex, out);
        }
        memcpy(out, req, 6);  // unit, fc, start, count
        return 6;
    }
    default: return exception(req, MB_EX_ILLEGAL_FUNCTION, out);
    }
}
