// GD25Q32E on SPI4 as a nor_t (components/ota/include/nor.h): 03h read, 20h sector erase, 02h page
// program, software chip select on PE4 (spec §3, §5.2). Used by the bootloader (staging, factory)
// and later by the app (FlashDB). Deadlines: twice the GD25Q32E Rev 1.3 §8.6 maxima.
#pragma once
#include "nor.h"
#include "stm32h7xx_hal.h"

#define SPI_NOR_JEDEC 0xC84016u

typedef struct {
    nor_t nor;  // first: a spi_nor_t * is a nor_t *
    SPI_HandleTypeDef *h;
    void (*kick)(void);  // while waiting (may be NULL)
} spi_nor_t;

// Waits for a write left running by a reset, checks the JEDEC id. NOR_OK, NOR_EIO (bus, wrong id)
// or NOR_ETIMEOUT.
int spi_nor_init(spi_nor_t *s, SPI_HandleTypeDef *h, void (*kick)(void));
uint32_t spi_nor_jedec(spi_nor_t *s);  // 0 on a bus error
