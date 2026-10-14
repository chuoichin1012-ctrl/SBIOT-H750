// XIP app entry (spec §6.2). The bootloader already runs PLL1 and maps the QSPI, so nothing here
// touches RCC clock setup, QUADSPI or pinmux. CubeMX's startup file still supplies the vector
// table; its weak Reset_Handler is replaced by this one, and its SystemInit is never called.
// This file is built with -fno-tree-loop-distribute-patterns: the copy loops must not become
// memcpy/memset calls, because those run from ITCM, which these loops fill.
#include <stdint.h>
#include "mpu_app.h"
#include "startup_app.h"
#include "stm32h7xx.h"

extern const uint32_t __app_copy_start__[], __app_copy_end__[];  // {load, run, bytes} triples
extern const uint32_t __app_zero_start__[], __app_zero_end__[];  // {run, bytes} pairs
extern const uint32_t g_pfnVectors[];
extern void __libc_init_array(void);
extern int main(void);

uint32_t g_handoff_faults;
uint32_t g_mpu_boot_match;

static uint32_t handoff_faults(void)
{
    uint32_t f = 0;
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL1)
        f |= HANDOFF_SYSCLK_NOT_PLL1;
    if ((QUADSPI->CCR & QUADSPI_CCR_FMODE) != QUADSPI_CCR_FMODE)
        f |= HANDOFF_QSPI_NOT_MAPPED;
    if (!__get_PRIMASK())
        f |= HANDOFF_IRQ_ENABLED;
    if (SysTick->CTRL & SysTick_CTRL_ENABLE_Msk)
        f |= HANDOFF_SYSTICK_ON;
    for (unsigned i = 0; i < sizeof NVIC->ISER / sizeof NVIC->ISER[0]; i++)
        if (NVIC->ISER[i] || NVIC->ISPR[i])
            f |= HANDOFF_NVIC_ACTIVE;
    return f;
}

void Reset_Handler(void)
{
    SCB->CPACR |= 0xFu << 20;  // CP10/CP11 full access: the FPU, before any code that may use it
    __DSB();
    __ISB();
    uint32_t faults = handoff_faults();  // before anything below changes that state
    RCC->AHB2ENR |= RCC_AHB2ENR_D2SRAM1EN | RCC_AHB2ENR_D2SRAM2EN | RCC_AHB2ENR_D2SRAM3EN;
    RCC->AHB4ENR |= RCC_AHB4ENR_BKPRAMEN;
    (void)RCC->AHB4ENR;  // the clocks are on before the first access
    SCB->VTOR = (uint32_t)g_pfnVectors;
    for (const uint32_t *t = __app_copy_start__; t < __app_copy_end__; t += 3) {
        const uint32_t *src = (const uint32_t *)t[0];
        uint32_t *dst = (uint32_t *)t[1];
        for (uint32_t n = t[2] / 4; n; n--)
            *dst++ = *src++;
    }
    for (const uint32_t *t = __app_zero_start__; t < __app_zero_end__; t += 2) {
        uint32_t *dst = (uint32_t *)t[0];
        for (uint32_t n = t[1] / 4; n; n--)
            *dst++ = 0;
    }
    g_handoff_faults = faults;
    g_mpu_boot_match = mpu_app_setup();
    SCB_EnableICache();
    SCB_EnableDCache();
    SystemCoreClockUpdate();  // reads RCC only
    __libc_init_array();
    main();
    for (;;) {
    }
}
