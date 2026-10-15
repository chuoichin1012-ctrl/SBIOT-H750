#include "io_mb.h"
#include <string.h>

static const io_mb_ops_t *s_ops;
static uint8_t s_addr;

static uint8_t read_bit(void *ctx, uint8_t fc, uint16_t a, bool *out)
{
    if (fc == 1 && a < s_ops->ndo)
        *out = s_ops->do_get(a);
    else if (fc == 2 && a < s_ops->ndi)
        *out = s_ops->di_get(a);
    else
        return MB_EX_ILLEGAL_ADDRESS;
    return 0;
}

static uint8_t read_reg(void *ctx, uint8_t fc, uint16_t a, uint16_t *out)
{
    int ch = a / 2;
    if (fc != 4 || ch >= s_ops->nai)
        return MB_EX_ILLEGAL_ADDRESS;
    if (!s_ops->ai_ok(ch))
        return MB_EX_DEVICE_FAILURE;  // the AI failed: no number to give
    float ma = (float)s_ops->ai_ua(ch) / 1000.0f;
    uint32_t w;
    memcpy(&w, &ma, 4);
    *out = (uint16_t)(a % 2 == 0 ? w >> 16 : w);  // ABCD: the high word first
    return 0;
}

static uint8_t write_bit(void *ctx, uint16_t a, bool on)
{
    if (a >= s_ops->ndo)
        return MB_EX_ILLEGAL_ADDRESS;
    return s_ops->do_set(a, on) == 0 ? 0 : MB_EX_DEVICE_FAILURE;
}

static bool exists(void *ctx, uint8_t fc, uint16_t a) { return fc == 1 && a < s_ops->ndo; }

static const mb_map_t k_map = {.read_bit = read_bit, .read_reg = read_reg, .write_bit = write_bit, .exists = exists};

void io_mb_init(const io_mb_ops_t *ops, uint8_t address)
{
    s_ops = ops;
    s_addr = address;
}

const mb_map_t *io_mb_map(void) { return &k_map; }

uint8_t io_mb_address(void) { return s_addr; }

size_t io_mb_remote(const uint8_t *req, size_t len, uint8_t *reply, size_t max)
{
    if (!s_ops || len < 4 || req[0] != s_addr || !mb_rtu_valid(req, len) || max < 8)
        return 0;
    size_t n = mb_serve(&k_map, req, len - 2, reply, max - 2);
    return n ? mb_rtu_seal(reply, n) : 0;
}
