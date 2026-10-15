// The SPI4 flash of the app (spec §5.2, §5.4, Plan 05b), one object for every user: the OTA
// staging writer and FlashDB. Operations of different tasks are serialised; a task waiting for
// the flash sleeps a tick per status poll, and data phases move by DMA through a 4 KB
// non-cacheable buffer in SRAM1 (never DMA into the caller's buffer, which may be DTCM or cached).
#pragma once
#include "spi_nor.h"

int flash_spi4_init(void);  // NOR_OK, or the error of spi_nor_init
nor_t *flash_spi4(void);
uint32_t flash_spi4_jedec(void);
unsigned flash_spi4_dma_errors(void);  // data phases that timed out or failed

#define FLASH_SPI4_SIZE 0x400000u  // GD25Q32E: 4 MB
/**
 * Provisioning (a board that ran another firmware): seals the chip, so every write of its users fails
 * from here on (an OTA upload into staging, FlashDB), then erases all of it. The caller resets the
 * board next. The sectors that failed to erase, or -1 when the flash never came up.
 */
int flash_spi4_wipe(void);
