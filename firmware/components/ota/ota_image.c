#include "ota_image.h"
#include <string.h>
#include "crc32.h"
#include "flashmap.h"
#include "sha256.h"

static const char k_magic[4] = {'S', 'B', 'I', 'O'};
static const char k_hw[16] = "SBIOT-H750";

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

int img_parse(const uint8_t raw[IMG_CORE], img_hdr_t *h)
{
    if (memcmp(raw, k_magic, 4))
        return IMG_EMAGIC;
    if (crc32_update(0, raw, 64) != le32(raw + 64))
        return IMG_ECRC;
    if (memcmp(raw + 4, k_hw, sizeof k_hw))
        return IMG_EHW;
    uint32_t size = le32(raw + 28);
    if (size < 8 || size > IMG_MAX_BODY)
        return IMG_ESIZE;
    memcpy(h->raw, raw, IMG_CORE);
    for (int i = 0; i < 3; i++)
        h->version[i] = (uint16_t)(raw[20 + 2 * i] | raw[21 + 2 * i] << 8);
    h->size = size;
    memcpy(h->sha256, raw + 32, 32);
    return IMG_OK;
}

void img_make(uint8_t raw[IMG_CORE], const uint16_t version[3], uint32_t size, const uint8_t sha256[32])
{
    memset(raw, 0, IMG_CORE);
    memcpy(raw, k_magic, 4);
    memcpy(raw + 4, k_hw, sizeof k_hw);
    for (int i = 0; i < 3; i++) {
        raw[20 + 2 * i] = (uint8_t)version[i];
        raw[21 + 2 * i] = (uint8_t)(version[i] >> 8);
    }
    put32(raw + 28, size);
    memcpy(raw + 32, sha256, 32);
    put32(raw + 64, crc32_update(0, raw, 64));
}

// An initial stack pointer is the end of the stack: above the region base, at most its end.
static bool in_ram(uint32_t sp, uint32_t base, uint32_t size)
{
    return sp > base && sp - base <= size;
}

bool img_vectors_ok(uint32_t msp, uint32_t reset, uint32_t size)
{
    bool msp_ok = (msp & 7u) == 0 && (in_ram(msp, 0x20000000u, 128u * 1024) || in_ram(msp, 0x24000000u, 512u * 1024));
    uint32_t pc = reset & ~1u, body = QSPI_XIP_BASE + IMG_HDR_SIZE;
    return msp_ok && (reset & 1u) && pc >= body && pc - body < size;
}

int img_check(nor_t *n, uint32_t off, void (*kick)(void), img_hdr_t *h)
{
    uint8_t raw[IMG_CORE];
    img_hdr_t hdr;
    if (n->read(n, off, raw, sizeof raw))
        return IMG_EREAD;
    int r = img_parse(raw, &hdr);
    if (r)
        return r;
    sha256_t s;
    sha256_init(&s);
    uint8_t buf[256];
    uint32_t vectors[2] = {0, 0};
    for (uint32_t done = 0; done < hdr.size;) {
        uint32_t len = hdr.size - done < sizeof buf ? hdr.size - done : (uint32_t)sizeof buf;
        if (n->read(n, off + IMG_HDR_SIZE + done, buf, len))
            return IMG_EREAD;
        if (done == 0)
            memcpy(vectors, buf, 8);  // size >= 8 (img_parse)
        sha256_update(&s, buf, len);
        done += len;
        if (kick && (done % NOR_SECTOR) == 0)
            kick();
    }
    uint8_t sha[32];
    sha256_final(&s, sha);
    if (memcmp(sha, hdr.sha256, 32))
        return IMG_ESHA;
    if (!img_vectors_ok(vectors[0], vectors[1], hdr.size))
        return IMG_EVECTORS;
    if (h)
        *h = hdr;
    return IMG_OK;
}
