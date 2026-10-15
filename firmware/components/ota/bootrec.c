#include "bootrec.h"
#include <string.h>
#include "crc32.h"
#include "flashmap.h"

#define REC_MAGIC 0x43455242u  // "BREC"
#define BODY_LEN (16 + 2 * IMG_CORE)

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void encode_body(const bootrec_t *r, uint8_t b[BODY_LEN])
{
    put32(b, REC_MAGIC);
    put32(b + 4, r->seq);
    uint8_t f[8] = {r->state, r->offset, r->trial, r->attempts, r->verify_fail, r->degraded, r->reason, 0};
    memcpy(b + 8, f, 8);
    memcpy(b + 16, r->staging, IMG_CORE);
    memcpy(b + 16 + IMG_CORE, r->backup, IMG_CORE);
}

void bootrec_encode(const bootrec_t *r, uint8_t out[512])
{
    memset(out, 0xFF, 512);
    encode_body(r, out);
    put32(out + 256, crc32_update(0, out, BODY_LEN));
    put32(out + 260, REC_MAGIC);
}

static bool decode(const uint8_t b[512], bootrec_t *r)
{
    if (le32(b) != REC_MAGIC || le32(b + 260) != REC_MAGIC || crc32_update(0, b, BODY_LEN) != le32(b + 256))
        return false;
    if (b[8] < BR_CONFIRMED || b[8] > BR_RESTORE)
        return false;
    r->seq = le32(b + 4);
    r->state = b[8];
    r->offset = b[9];
    r->trial = b[10];
    r->attempts = b[11];
    r->verify_fail = b[12];
    r->degraded = b[13];
    r->reason = b[14];
    memcpy(r->staging, b + 16, IMG_CORE);
    memcpy(r->backup, b + 16 + IMG_CORE, IMG_CORE);
    return true;
}

// 1 valid, 0 invalid, < 0 read error
static int read_slot(nor_t *spi, uint32_t off, bootrec_t *r)
{
    uint8_t b[512];
    int e = spi->read(spi, off, b, sizeof b);
    return e ? e : decode(b, r);
}

static bool newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

// Which slot holds the newest record (*at = SPI_REC_A or SPI_REC_B, 0 when neither is valid);
// returns a NOR error when either copy cannot be read.
static int newest(nor_t *spi, bootrec_t *r, uint32_t *at)
{
    bootrec_t a, b;
    int va = read_slot(spi, SPI_REC_A, &a), vb = read_slot(spi, SPI_REC_B, &b);
    if (va < 0)
        return va;
    if (vb < 0)
        return vb;
    *at = 0;
    if (va && (!vb || !newer(b.seq, a.seq))) {
        *r = a;
        *at = SPI_REC_A;
    } else if (vb) {
        *r = b;
        *at = SPI_REC_B;
    }
    return NOR_OK;
}

int bootrec_read(nor_t *spi, bootrec_t *r)
{
    uint32_t at;
    int e = newest(spi, r, &at);
    return e ? e : at ? BOOTREC_VALID : BOOTREC_NONE;
}

int bootrec_write(nor_t *spi, bootrec_t *r)
{
    bootrec_t cur;
    uint32_t at;
    int e = newest(spi, &cur, &at);
    if (e)
        return e;
    uint32_t slot = at == SPI_REC_A ? SPI_REC_B : SPI_REC_A;
    r->seq = at ? cur.seq + 1 : 1;
    uint8_t b[512], back[512];
    bootrec_encode(r, b);
    e = spi->erase_sector(spi, slot);
    if (!e)
        e = spi->program(spi, slot, b, NOR_PAGE);  // body first
    if (!e)
        e = spi->program(spi, slot + NOR_PAGE, b + NOR_PAGE, NOR_PAGE);  // the CRC page last
    if (!e)
        e = spi->read(spi, slot, back, sizeof back);
    if (!e && memcmp(b, back, sizeof b))
        e = NOR_EIO;
    return e;
}
