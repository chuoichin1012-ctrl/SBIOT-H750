#include "install.h"
#include <string.h>
#include "flashmap.h"
#include "ota_image.h"

static void kick(install_t *in)
{
    if (in->kick)
        in->kick();
}

// Programs [lo, hi) of the image from src at so to dst at dof, page by page.
static int copy_range(install_t *in, nor_t *src, uint32_t so, nor_t *dst, uint32_t dof, uint32_t lo, uint32_t hi)
{
    uint8_t buf[NOR_PAGE];
    for (uint32_t p = lo; p < hi; p += NOR_PAGE) {
        uint32_t n = hi - p < NOR_PAGE ? hi - p : NOR_PAGE;
        if (src->read(src, so + p, buf, n))
            return -1;
        if (p == 0)
            memset(buf, 0xFF, sizeof IMG_MAGIC);  // the magic is programmed last, on its own
        if (dst->program(dst, dof + p, buf, n))
            return -1;
    }
    return 0;
}

// Reads back [0, len) of the destination against the source; the magic must still be blank.
static int same(install_t *in, nor_t *src, uint32_t so, nor_t *dst, uint32_t dof, uint32_t len)
{
    uint8_t a[NOR_PAGE], b[NOR_PAGE];
    for (uint32_t p = 0; p < len; p += NOR_PAGE) {
        uint32_t n = len - p < NOR_PAGE ? len - p : NOR_PAGE;
        if (src->read(src, so + p, a, n) || dst->read(dst, dof + p, b, n))
            return -1;
        if (p == 0)
            memset(a, 0xFF, sizeof IMG_MAGIC);
        if (memcmp(a, b, n))
            return -1;
        if (p % NOR_SECTOR == 0)
            kick(in);
    }
    return 0;
}

// Copies the image h (already checked at src/so) to dst/dof (spec §11.2): the first sector is
// erased first, so a destination cut short never passes img_check; then every other sector is
// erased and programmed; then the first sector. The destination is read back against the source,
// and only then gets the magic, as the very last program: a destination cut short or programmed
// wrong never has a magic. The source is never written. Returns 0 when the destination then passes
// img_check with the source's header.
static int copy(install_t *in, nor_t *src, uint32_t so, const img_hdr_t *h, nor_t *dst, uint32_t dof)
{
    uint32_t len = IMG_HDR_SIZE + h->size;
    if (dst->erase_sector(dst, dof))
        return -1;
    for (uint32_t s = NOR_SECTOR; s < len; s += NOR_SECTOR) {
        if (dst->erase_sector(dst, dof + s))
            return -1;
        if (copy_range(in, src, so, dst, dof, s, len - s < NOR_SECTOR ? len : s + NOR_SECTOR))
            return -1;
        kick(in);
    }
    if (copy_range(in, src, so, dst, dof, 0, len < NOR_SECTOR ? len : NOR_SECTOR))
        return -1;
    if (same(in, src, so, dst, dof, len))
        return -1;
    if (dst->program(dst, dof, IMG_MAGIC, sizeof IMG_MAGIC))
        return -1;
    img_hdr_t back;
    if (img_check(dst, dof, in->kick, &back) != IMG_OK || memcmp(back.raw, h->raw, IMG_CORE))
        return -1;
    return 0;
}

static install_result_t halt(install_t *in, install_why_t why)
{
    in->why = why;
    return INSTALL_HALT;
}

// The factory slot has never been seeded: no magic. A read error counts as seeded (never write
// over what cannot be read).
static bool factory_unseeded(install_t *in)
{
    uint8_t m[sizeof IMG_MAGIC];
    return in->spi->read(in->spi, SPI_FACTORY, m, sizeof m) == NOR_OK && memcmp(m, IMG_MAGIC, sizeof m);
}

install_result_t install_run(install_t *in)
{
    img_hdr_t hq, hs, hf;
    in->did = 0;
    in->why = WHY_NONE;

    if (in->reload) {  // restore the factory image
        if (img_check(in->spi, SPI_FACTORY, in->kick, &hf) != IMG_OK)
            return halt(in, WHY_FACTORY_BAD);
        if (in->spi->erase_sector(in->spi, SPI_STAGING))  // staging's image is never installed again
            return halt(in, WHY_STAGING);
        if (copy(in, in->spi, SPI_FACTORY, &hf, in->qspi, QSPI_SLOT))
            return halt(in, WHY_COPY);
        in->did |= DID_RESTORE;
        return INSTALL_JUMP;
    }

    bool q = img_check(in->qspi, QSPI_SLOT, in->kick, &hq) == IMG_OK;
    if (q && factory_unseeded(in))  // once, at the first boot after production
        in->did |= copy(in, in->qspi, QSPI_SLOT, &hq, in->spi, SPI_FACTORY) ? DID_SEED_FAIL : DID_SEED;

    if (img_check(in->spi, SPI_STAGING, in->kick, &hs) == IMG_OK && !(q && !memcmp(hq.raw, hs.raw, IMG_CORE))) {
        if (copy(in, in->spi, SPI_STAGING, &hs, in->qspi, QSPI_SLOT))
            return halt(in, WHY_COPY);
        in->did |= DID_INSTALL;
        return INSTALL_JUMP;
    }
    return q ? INSTALL_JUMP : halt(in, WHY_NO_IMAGE);
}
