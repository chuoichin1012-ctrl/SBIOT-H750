// The flash under FlashDB (spec §9.1): set before store_init mounts the databases.
#pragma once
#include "nor.h"

void fal_nor_set(nor_t *nor);
