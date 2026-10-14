// The app's only MPU programming (spec §6.2 step 4), done by Reset_Handler before any DMA exists.
#pragma once
#include <stdint.h>

// Returns 1 when the bootloader's regions 0 and 1 match the app's, then programs the whole table.
uint32_t mpu_app_setup(void);
