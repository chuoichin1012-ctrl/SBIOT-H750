// Where the OTA data live (spec §5.1, §5.2). Offsets are device offsets, not CPU addresses.
#pragma once

#define SLOT_SIZE 0x170000u  // 1 472 KB = 23 x 64 KB: an image (1 KB header + body) at most

// QSPI (GD25Q32E on bank 2): the running image at offset 0, executed at 0x90000000.
#define QSPI_SLOT 0x000000u
#define QSPI_XIP_BASE 0x90000000u

// SPI4 (GD25Q32E): the staging slot (new image from OTA) and the factory slot (seeded once from
// the first image in QSPI, then only read).
#define SPI_STAGING 0x110000u
#define SPI_FACTORY 0x280000u

// QSPI: the anti-clone binding record (spec §11.5), one 4 KB sector outside the slot.
#define QSPI_BIND 0x3F0000u
