// FAL layout of FlashDB's 1 MB at SPI4 offset 0 (spec §5.2). The device is a nor_t (fal_nor.c);
// sector sizes are set per database in store.c.
#pragma once

#define FAL_PART_HAS_TABLE_CFG
#ifdef __arm__
#include "blog.h"
#define FAL_PRINTF(...) blog(__VA_ARGS__)
#endif
#define STORE_FAL_DEV "spi4"

extern const struct fal_flash_dev store_fal_dev;

#define FAL_FLASH_DEV_TABLE {&store_fal_dev}

#define FAL_PART_TABLE                                                       \
    {                                                                        \
        {FAL_PART_MAGIC_WORD, "cfg", STORE_FAL_DEV, 0x00000, 0x40000, 0},    \
        {FAL_PART_MAGIC_WORD, "edge", STORE_FAL_DEV, 0x40000, 0x40000, 0},   \
        {FAL_PART_MAGIC_WORD, "cert", STORE_FAL_DEV, 0x80000, 0x20000, 0},   \
        {FAL_PART_MAGIC_WORD, "log", STORE_FAL_DEV, 0xA0000, 0x60000, 0},    \
    }
