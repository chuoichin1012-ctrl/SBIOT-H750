// Bootloader entry points, called from the USER CODE blocks of cube/boot/Src/main.c.
#pragma once

#define BOOT_VERSION "0.2.0"

void boot_early(void);  // USER CODE 1: before MPU_Config, HAL_Init and the clocks (spec §11.2 steps 1-2)
void boot_main(void);   // USER CODE 2: after the CubeMX init calls; never returns
