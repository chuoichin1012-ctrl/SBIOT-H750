#include "ota_app.h"
#include <string.h>
#include "flashmap.h"

static int fail(ota_stage_t *st, int e)
{
    if (!st->error)
        st->error = e;
    return st->error;
}

int ota_stage_begin(ota_stage_t *st, nor_t *spi, uint32_t total)
{
    memset(st, 0, sizeof *st);
    st->spi = spi;
    st->total = total;
    if (total > SLOT_SIZE)
        return fail(st, OTA_ETOOBIG);
    if (total < IMG_HDR_SIZE + 8)
        return fail(st, OTA_EIMAGE);
    // The image staged before is invalid from now on, whatever this upload becomes. The magic is
    // read back: GD25Q32E ignores an erase of a protected sector and reports nothing (§7.15).
    static const uint8_t erased[sizeof IMG_MAGIC] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t m[sizeof IMG_MAGIC];
    if (spi->erase_sector(spi, SPI_STAGING) || spi->read(spi, SPI_STAGING, m, sizeof m) || memcmp(m, erased, sizeof m))
        return fail(st, OTA_EFLASH);
    st->erased = NOR_SECTOR;
    sha256_init(&st->sha);
    return OTA_OK;
}

static int flush_page(ota_stage_t *st)
{
    uint32_t at = st->got - st->fill;  // image offset of page[0]; a page boundary
    while (st->erased < at + st->fill) {
        if (st->spi->erase_sector(st->spi, SPI_STAGING + st->erased))
            return fail(st, OTA_EFLASH);
        st->erased += NOR_SECTOR;
    }
    if (at == 0)
        memset(st->page, 0xFF, sizeof IMG_MAGIC);  // the magic only once the whole image is in (finish)
    if (st->spi->program(st->spi, SPI_STAGING + at, st->page, st->fill))
        return fail(st, OTA_EFLASH);
    st->fill = 0;
    return OTA_OK;
}

int ota_stage_write(ota_stage_t *st, const void *data, uint32_t len)
{
    const uint8_t *p = data;
    if (st->error)
        return st->error;
    if (len > st->total - st->got)
        return fail(st, OTA_ETOOBIG);
    while (len) {
        uint32_t n = NOR_PAGE - st->fill < len ? NOR_PAGE - st->fill : len;
        memcpy(st->page + st->fill, p, n);
        uint32_t before = st->got;
        st->fill += n;
        st->got += n;
        if (before < IMG_CORE && st->got >= IMG_CORE) {  // the header core is complete: page[0..67]
            if (img_parse(st->page, &st->hdr) != IMG_OK)
                return fail(st, OTA_EIMAGE);
            if (IMG_HDR_SIZE + st->hdr.size != st->total)
                return fail(st, OTA_EIMAGE);
        }
        // the body starts at IMG_HDR_SIZE, a page boundary
        if (st->got > IMG_HDR_SIZE) {
            uint32_t body_from = before > IMG_HDR_SIZE ? before : IMG_HDR_SIZE;
            sha256_update(&st->sha, st->page + (body_from - (st->got - st->fill)), st->got - body_from);
        }
        if (st->fill == NOR_PAGE && flush_page(st))
            return st->error;
        p += n;
        len -= n;
    }
    return OTA_OK;
}

int ota_stage_finish(ota_stage_t *st)
{
    if (st->error)
        return st->error;
    if (st->got < st->total)
        return fail(st, OTA_ESHORT);
    if (st->fill && flush_page(st))
        return st->error;
    uint8_t sha[32];
    sha256_final(&st->sha, sha);
    if (memcmp(sha, st->hdr.sha256, 32))
        return fail(st, OTA_EIMAGE);
    if (st->spi->program(st->spi, SPI_STAGING, IMG_MAGIC, sizeof IMG_MAGIC))
        return fail(st, OTA_EFLASH);
    // What the bootloader will check, read back from staging itself (catches programming errors).
    img_hdr_t back;
    int r = img_check(st->spi, SPI_STAGING, 0, &back);
    if (r == IMG_EVECTORS)
        return fail(st, OTA_EIMAGE);
    if (r != IMG_OK || memcmp(back.raw, st->hdr.raw, IMG_CORE))
        return fail(st, OTA_EFLASH);
    return OTA_OK;
}
