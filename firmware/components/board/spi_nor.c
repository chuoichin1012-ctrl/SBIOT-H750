#include <stdbool.h>
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

// One command: header out, then data out (tx) or in (rx), chip select low throughout. A long
// data phase goes through the port when it has one (DMA in the app).
static int xfer(spi_nor_t *s, const uint8_t *hdr, uint16_t hn, const uint8_t *tx, uint8_t *rx, uint16_t n)
{
    chip_select(1);
    int r = HAL_SPI_Transmit(s->h, (uint8_t *)hdr, hn, PHASE_MS) == HAL_OK ? NOR_OK : NOR_EIO;
    if (!r && n) {
        if (s->port && s->port->data && n >= SPI_NOR_DMA_MIN)
            r = s->port->data(s->h, tx, rx, n);
        else
            r = (tx ? HAL_SPI_Transmit(s->h, (uint8_t *)tx, n, PHASE_MS) : HAL_SPI_Receive(s->h, rx, n, PHASE_MS)) == HAL_OK
                    ? NOR_OK
                    : NOR_EIO;
    }
    chip_select(0);  // only once the transfer has ended (or was aborted)
    return r;
}

static void lock(spi_nor_t *s)
{
    if (s->port && s->port->lock)
        s->port->lock();
}

static void unlock(spi_nor_t *s)
{
    if (s->port && s->port->unlock)
        s->port->unlock();
}

static int wait_ready(spi_nor_t *s, uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    for (;;) {
        // The clock is read before the status, so a timeout means still busy after the deadline. Read after it, a
        // task that lost the CPU between the two (to TLS handshakes, Plan 09d) took an old busy status for a timeout.
        bool late = HAL_GetTick() - t0 > ms;
        uint8_t c = 0x05, sr1;
        if (xfer(s, &c, 1, 0, &sr1, 1))
            return NOR_EIO;
        if (!(sr1 & 0x01))
            return NOR_OK;
        if (late)
            return NOR_ETIMEOUT;
        if (s->kick)
            s->kick();
        if (s->port && s->port->busy_wait)
            s->port->busy_wait();  // the app sleeps a tick instead of spinning
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
    int r = NOR_OK;
    lock(s);
    while (len && !r) {
        uint16_t k = (uint16_t)(len < CHUNK ? len : CHUNK);
        uint8_t hdr[4];
        addr_cmd(hdr, 0x03, off);
        r = xfer(s, hdr, 4, 0, p, k);
        off += k;
        p += k;
        len -= k;
    }
    unlock(s);
    return r;
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
    lock(s);
    int r = write_enable(s) || xfer(s, hdr, 4, 0, 0, 0) ? NOR_EIO : wait_ready(s, ERASE_MS);
    unlock(s);
    return r;
}

static int nor_program(nor_t *n, uint32_t off, const void *buf, uint32_t len)
{
    spi_nor_t *s = (spi_nor_t *)n;
    if (len == 0 || len > NOR_PAGE || off / NOR_PAGE != (off + len - 1) / NOR_PAGE)
        return NOR_EIO;
    uint8_t hdr[4];
    addr_cmd(hdr, 0x02, off);
    lock(s);
    int r = write_enable(s) || xfer(s, hdr, 4, buf, 0, (uint16_t)len) ? NOR_EIO : wait_ready(s, PROGRAM_MS);
    unlock(s);
    return r;
}

uint32_t spi_nor_jedec(spi_nor_t *s)
{
    uint8_t c = 0x9F, id[3];
    if (xfer(s, &c, 1, 0, id, 3))
        return 0;
    return (uint32_t)id[0] << 16 | (uint32_t)id[1] << 8 | id[2];
}

int spi_nor_init(spi_nor_t *s, SPI_HandleTypeDef *h, void (*kick)(void), const spi_nor_port_t *port)
{
    s->port = port;
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
