// Where the OTA data live (spec §5.1, §5.2). Offsets are device offsets, not CPU addresses.
#pragma once

#define SLOT_SIZE 0x170000u  // 1 472 KB = 23 x 64 KB: an image (1 KB header + body) at most
#define CHECKPOINT 0x10000u  // 64 KB: the unit of BACKUP/WRITE/RESTORE progress (spec §5.2)

// QSPI (GD25Q32E on bank 2): the running image at offset 0, executed at 0x90000000.
#define QSPI_SLOT 0x000000u
#define QSPI_XIP_BASE 0x90000000u

// SPI4 (GD25Q32E): boot state record A/B, staging and backup slots.
#define SPI_REC_A 0x100000u
#define SPI_REC_B 0x101000u
#define SPI_STAGING 0x110000u
#define SPI_BACKUP 0x280000u
