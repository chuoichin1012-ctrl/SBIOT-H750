// What Reset_Handler saw and did before main() (spec §6.2), for the boot log.
#pragma once
#include <stdint.h>

// Bits of the handoff state that were NOT as the bootloader promises (0 = all as specified).
enum {
    HANDOFF_SYSCLK_NOT_PLL1 = 1u << 0,
    HANDOFF_QSPI_NOT_MAPPED = 1u << 1,
    HANDOFF_IRQ_ENABLED = 1u << 2,
    HANDOFF_SYSTICK_ON = 1u << 3,
    HANDOFF_NVIC_ACTIVE = 1u << 4,
};
extern uint32_t g_handoff_faults;
// 1 when MPU regions 0 and 1 left by the bootloader equal the app's own (spec §6.2).
extern uint32_t g_mpu_boot_match;
