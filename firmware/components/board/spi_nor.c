#include "spi_nor.h"
#include "main.h"  // SPI4_NSS pin label

#define PHASE_MS 100u
#define ERASE_MS 600u    // 2 x tSE 300 ms
#define PROGRAM_MS 6u    // 2 x tPP 2.4 ms, plus one tick of HAL_GetTick resolution
#define CHUNK 4096u      // bytes per read command (HAL sizes are 16-bit)

static void chip_select(int on)  // NSS is active low
{
    HAL_GPIO_WritePin(SPI4_NSS_GPIO_Port, SPI4_NSS_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

// One command: header out, then data out (tx) or in (rx), chip select low throughout.
static int xfer(spi_nor_t *s, const uint8_t *hdr, uint16_t hn, const uint8_t *tx, uint8_t *rx, uint16_t n)
{
    chip_select(1);
    int ok = HAL_SPI_Transmit(s->h, (uint8_t *)hdr, hn, PHASE_MS) == HAL_OK;
    if (ok && n)
        ok = tx ? HAL_SPI_Transmit(s->h, (uint8_t *)tx, n, PHASE_MS) == HAL_OK
                : HAL_SPI_Receive(s->h, rx, n, PHASE_MS) == HAL_OK;
    chip_select(0);
    return ok ? NOR_OK : NOR_EIO;
}

static int wait_ready(spi_nor_t *s, uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    for (;;) {
        uint8_t c = 0x05, sr1;
        if (xfer(s, &c, 1, 0, &sr1, 1))
            return NOR_EIO;
        if (!(sr1 & 0x01))
            return NOR_OK;
        if (s->kick)
            s->kick();
        if (HAL_GetTick() - t0 > ms)
            return NOR_ETIMEOUT;
    }
}

static void addr_cmd(uint8_t out[4], uint8_t ins, uint32_t off)
{
    out[0] = ins;
    out[1] = (uint8_t)(off >> 16);
    out[2] = (uint8_t)(off >> 8);
    out[3] = (uint8_t)off;
}

static int nor_read(nor_t *n, uint32_t off, void *buf, uint32_t len)
{
    spi_nor_t *s = (spi_nor_t *)n;
    uint8_t *p = buf;
    while (len) {
        uint16_t k = (uint16_t)(len < CHUNK ? len : CHUNK);
        uint8_t hdr[4];
        addr_cmd(hdr, 0x03, off);
        if (xfer(s, hdr, 4, 0, p, k))
            return NOR_EIO;
        off += k;
        p += k;
        len -= k;
    }
    return NOR_OK;
}

static int write_enable(spi_nor_t *s)
{
    uint8_t c = 0x06;
    return xfer(s, &c, 1, 0, 0, 0);
}

static int nor_erase(nor_t *n, uint32_t off)
{
    spi_nor_t *s = (spi_nor_t *)n;
    uint8_t hdr[4];
    addr_cmd(hdr, 0x20, off);
    if (write_enable(s) || xfer(s, hdr, 4, 0, 0, 0))
        return NOR_EIO;
    return wait_ready(s, ERASE_MS);
}

static int nor_program(nor_t *n, uint32_t off, const void *buf, uint32_t len)
{
    spi_nor_t *s = (spi_nor_t *)n;
    if (len == 0 || len > NOR_PAGE || off / NOR_PAGE != (off + len - 1) / NOR_PAGE)
        return NOR_EIO;
    uint8_t hdr[4];
    addr_cmd(hdr, 0x02, off);
    if (write_enable(s) || xfer(s, hdr, 4, buf, 0, (uint16_t)len))
        return NOR_EIO;
    return wait_ready(s, PROGRAM_MS);
}

uint32_t spi_nor_jedec(spi_nor_t *s)
{
    uint8_t c = 0x9F, id[3];
    if (xfer(s, &c, 1, 0, id, 3))
        return 0;
    return (uint32_t)id[0] << 16 | (uint32_t)id[1] << 8 | id[2];
}

int spi_nor_init(spi_nor_t *s, SPI_HandleTypeDef *h, void (*kick)(void))
{
    s->nor.read = nor_read;
    s->nor.erase_sector = nor_erase;
    s->nor.program = nor_program;
    s->nor.ctx = s;
    s->h = h;
    s->kick = kick;
    int r = wait_ready(s, ERASE_MS);  // a reset may have come in the middle of an erase
    if (r)
        return r;
    return spi_nor_jedec(s) == SPI_NOR_JEDEC ? NOR_OK : NOR_EIO;
}
