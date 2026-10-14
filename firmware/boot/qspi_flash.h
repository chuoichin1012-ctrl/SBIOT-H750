// GD25Q32E on QUADSPI bank 2 (spec §6.2): bring the flash to a known state, enable quad mode and map
// it at 0x90000000 with Quad I/O Fast Read (EBh). Values from GD25Q32E Rev 1.3 §6, §7.11, §7.29, §8.6.
#pragma once
#include <stdint.h>
#include "stm32h7xx_hal.h"

#define QF_JEDEC_GD25Q32E 0xC84016u

enum { QF_OK = 0, QF_EHAL = -1, QF_EID = -2, QF_EQE = -3, QF_ETIMEOUT = -4 };

typedef void (*qf_kick_fn)(void);

// ES0392 Rev 15 §2.7.4 "QUADSPI internal timing criticality": the errata's register sequence. Run it
// after every reset with the QUADSPI kernel clock on (the bootloader's HAL_QSPI_MspInit), and, from
// Plan 04 on, whenever the QUADSPI leaves memory-mapped mode. It leaves the QUADSPI disabled.
void qf_errata_timing(void);

// Leaves continuous-read mode, resets the flash (66h/99h), checks the JEDEC id, sets QE if clear
// and reads the dummy configuration. kick is called while waiting.
int qf_init(QSPI_HandleTypeDef *h, qf_kick_fn kick);
uint32_t qf_read_jedec(QSPI_HandleTypeDef *h);  // 0 on a bus error
int qf_memory_mapped(QSPI_HandleTypeDef *h);
