// FlashDB build configuration (spec §9.1).
#pragma once

#define FDB_USING_KVDB
#define FDB_USING_TSDB
#define FDB_USING_FAL_MODE
#define FDB_WRITE_GRAN 1                // NOR flash: any bit can be cleared on its own
#define FDB_USING_TIMESTAMP_64BIT       // the TSDB key is a 64-bit sequence number
// One sector cache entry per sector of the largest KVDB (cfg and edge: 256 KB / 16 KB). With fewer,
// finding room for a write scans the KVs of the uncached sectors, every one a SPI read: a save took
// about 210 ms on the board once cfg was full of old values (Plan 05b), the default being 8.
#define FDB_SECTOR_CACHE_TABLE_SIZE 16
#ifdef __arm__  // the board: newlib's printf goes nowhere (syscalls.c), the log goes to UART7
#include "blog.h"
#define FDB_PRINT(...) blog(__VA_ARGS__)
#else
#include <stdio.h>
#define FDB_PRINT(...) printf(__VA_ARGS__)
#endif
