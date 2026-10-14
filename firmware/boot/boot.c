// Bootloader stub of Plan 02 (spec §6.2, §11.2 boot sequence): watchdogs, safe outputs, clocks
// (CubeMX), QSPI in memory-mapped mode, then the jump to the XIP app. The OTA state machine of
// §11.2 replaces boot_main's middle part in Plan 04.
#include "boot.h"
#include <stdarg.h>
#include <stdio.h>
#include "board.h"
#include "gpio.h"
#include "jumpcheck.h"
#include "main.h"
#include "qspi_flash.h"
#include "quadspi.h"
#include "usart.h"

static void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_line(const char *fmt, ...)
{
    char line[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 1)
        n = (int)sizeof line - 1;
    if (n > 0)
        HAL_UART_Transmit(&huart7, (const uint8_t *)line, (uint16_t)n, 100);
}

// Halted rescue (spec §11.2): LED WORK blinks fast, both watchdogs stay fed, SWD can reflash.
static void __attribute__((noreturn)) rescue(const char *why, int code)
{
    log_line("boot: %s (%d), halted rescue\r\n", why, code);
    for (;;) {
        board_kick();
        HAL_Delay(100);
    }
}

// Hands over in the state of spec §6.2: interrupts off, SysTick off, nothing enabled or pending.
static void __attribute__((noreturn)) jump(uint32_t msp, uint32_t pc)
{
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    for (unsigned i = 0; i < sizeof NVIC->ICER / sizeof NVIC->ICER[0]; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    __DSB();
    __ISB();
    SCB->VTOR = APP_VECTORS;
    __asm volatile("msr msp, %0\n\tbx %1" : : "r"(msp), "r"(pc) : "memory");
    __builtin_unreachable();
}

void boot_early(void)
{
    MX_GPIO_Init();  // boot.ioc levels: latch OE high (hi-Z), D1/D2/LE low, modem keys low, PHY in reset
    board_kick();
}

void boot_main(void)
{
    board_kick();
    board_latch_zero();
    DBGMCU->APB4FZ1 |= DBGMCU_APB4FZ1_DBG_IWDG1;  // a core halted by the debugger does not trip IWDG1
    log_line("\r\nSBIOT-H750 boot " BOOT_VERSION " rsr=%08lx sysclk=%lu\r\n", (unsigned long)RCC->RSR,
             (unsigned long)HAL_RCC_GetSysClockFreq());
    int r = qf_init(&hqspi, board_kick);
    log_line("boot: qspi id %06lx\r\n", (unsigned long)qf_read_jedec(&hqspi));
    if (r)
        rescue("qspi init failed", r);
    r = qf_memory_mapped(&hqspi);
    if (r)
        rescue("qspi memory-mapped failed", r);
    const volatile uint32_t *v = (const volatile uint32_t *)APP_VECTORS;
    uint32_t msp = v[0], pc = v[1];
    if (!boot_app_ok(msp, pc)) {
        log_line("boot: no valid app (msp %08lx, reset %08lx)\r\n", (unsigned long)msp, (unsigned long)pc);
        rescue("no app", 0);
    }
    log_line("boot: jump msp %08lx pc %08lx\r\n", (unsigned long)msp, (unsigned long)pc);
    while (!(huart7.Instance->ISR & USART_ISR_TC)) {
    }
    board_kick();
    jump(msp, pc);
}
