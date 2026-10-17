// The flash under FlashDB (spec §9.1): set before store_init mounts the databases.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "nor.h"

void fal_nor_set(nor_t *nor);

// A read that failed after its retries, or a program or erase that failed, makes the flash read-only
// (no write, no erase) until fal_nor_rearm() at the next boot. FlashDB acts on what it reads, so it must
// never write after a misread; and after a failed sector-status write it would go on filling a sector
// the next mount takes for empty (Plan 09b T2, Codex): nothing more is written this boot.
bool fal_nor_read_failed(void);
void fal_nor_rearm(void);
/** Sectors erased since boot (store_progress, review 3 P2-3). */
uint32_t fal_nor_erased(void);
