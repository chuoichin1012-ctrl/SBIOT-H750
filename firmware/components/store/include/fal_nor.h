// The flash under FlashDB (spec §9.1): set before store_init mounts the databases.
#pragma once
#include <stdbool.h>
#include "nor.h"

void fal_nor_set(nor_t *nor);

// A read that failed after its retries makes the flash read-only (no write, no erase) until
// fal_nor_rearm(): FlashDB acts on what it reads, so it must never write after a misread.
bool fal_nor_read_failed(void);
void fal_nor_rearm(void);
