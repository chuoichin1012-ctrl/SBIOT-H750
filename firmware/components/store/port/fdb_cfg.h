// FlashDB build configuration (spec §9.1).
#pragma once
#include <stdio.h>

#define FDB_USING_KVDB
#define FDB_USING_TSDB
#define FDB_USING_FAL_MODE
#define FDB_WRITE_GRAN 1                // NOR flash: any bit can be cleared on its own
#define FDB_USING_TIMESTAMP_64BIT       // the TSDB key is a 64-bit sequence number
#define FDB_PRINT(...) printf(__VA_ARGS__)
