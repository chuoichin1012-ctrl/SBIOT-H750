#include "mpu_app.h"
#include "stm32h7xx.h"

// Regions 0 and 1 repeat the bootloader's MPU_Config (cube/boot, from boot.ioc):
//   0: CubeMX background region, 4 GB with sub-regions 0, 1, 2 and 7 disabled, so
//      0x60000000-0xDFFFFFFF is no-access and never fetched speculatively;
//   1: the 4 MB QSPI window, read-only, cacheable, executable.
// A higher region number wins where regions overlap.
static const ARM_MPU_Region_t k_regions[] = {
    {ARM_MPU_RBAR(0, 0x00000000u), ARM_MPU_RASR(1, ARM_MPU_AP_NONE, 0, 1, 0, 0, 0x87, ARM_MPU_REGION_SIZE_4GB)},
    {ARM_MPU_RBAR(1, 0x90000000u), ARM_MPU_RASR(0, ARM_MPU_AP_RO, 0, 0, 1, 0, 0x00, ARM_MPU_REGION_SIZE_4MB)},
    // SRAM1: DMA buffers (spec §5.3, §5.4), Normal non-cacheable (TEX 1, C 0, B 0), never executed
    {ARM_MPU_RBAR(2, 0x30000000u), ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 0, 0, 0, 0x00, ARM_MPU_REGION_SIZE_128KB)},
    // the ADC3 BDMA buffer (spec §6.6), Normal non-cacheable, never executed
    {ARM_MPU_RBAR(3, 0x3800FC00u), ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 0, 0, 0, 0x00, ARM_MPU_REGION_SIZE_1KB)},
    // null-pointer guard: the first 1 KB of ITCM; .itcm_text starts at 0x400
    {ARM_MPU_RBAR(4, 0x00000000u), ARM_MPU_RASR(1, ARM_MPU_AP_NONE, 0, 0, 0, 0, 0x00, ARM_MPU_REGION_SIZE_1KB)},
};

uint32_t mpu_app_setup(void)
{
    uint32_t match = 1;
    for (uint32_t i = 0; i < 2; i++) {
        MPU->RNR = i;
        if ((MPU->RBAR & MPU_RBAR_ADDR_Msk) != (k_regions[i].RBAR & MPU_RBAR_ADDR_Msk) || MPU->RASR != k_regions[i].RASR)
            match = 0;
    }
    ARM_MPU_Disable();
    for (uint32_t i = 0; i < 16; i++)
        ARM_MPU_ClrRegion(i);
    ARM_MPU_Load(k_regions, sizeof k_regions / sizeof k_regions[0]);
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
    return match;
}
