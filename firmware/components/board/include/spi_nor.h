// GD25Q32E on SPI4 as a nor_t (components/ota/include/nor.h): 03h read, 20h sector erase, 02h page
// program, software chip select on PE4 (spec §3, §5.2). Used by the bootloader (staging, factory)
// and later by the app (FlashDB). Deadlines: twice the GD25Q32E Rev 1.3 §8.6 maxima.
#pragma once
#include "nor.h"
#include "stm32h7xx_hal.h"

#define SPI_NOR_JEDEC 0xC84016u

#define SPI_NOR_DMA_MIN 256u  // data phases this long or longer go through port->data (a page, a 4 KB read)

// What the platform adds (all optional; NULL members or a NULL port: the bootloader's polling).
// The app (Plan 05b) blocks instead of spinning: it sleeps a tick while the flash is busy, moves
// data phases by DMA through its own buffer, and serialises operations of its tasks.
typedef struct {
    void (*lock)(void);  // around each read, erase and program
    void (*unlock)(void);
    void (*busy_wait)(void);  // between two status polls while the flash is busy
    // One data phase with chip select already low: n bytes out of tx, or into rx. NOR_OK, NOR_EIO,
    // or NOR_ETIMEOUT after the transfer was aborted.
    int (*data)(SPI_HandleTypeDef *h, const uint8_t *tx, uint8_t *rx, uint32_t n);
} spi_nor_port_t;

typedef struct {
    nor_t nor;  // first: a spi_nor_t * is a nor_t *
    SPI_HandleTypeDef *h;
    void (*kick)(void);  // while waiting (may be NULL)
    const spi_nor_port_t *port;
} spi_nor_t;

// Waits for a write left running by a reset, checks the JEDEC id. NOR_OK, NOR_EIO (bus, wrong id)
// or NOR_ETIMEOUT. port may be NULL.
int spi_nor_init(spi_nor_t *s, SPI_HandleTypeDef *h, void (*kick)(void), const spi_nor_port_t *port);
uint32_t spi_nor_jedec(spi_nor_t *s);  // 0 on a bus error
