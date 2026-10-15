// Bootloader (spec §6.2, §11.2, §11.5): watchdogs, safe outputs, clocks (CubeMX), QSPI and SPI4,
// the anti-clone check, the Reload button, the install logic, then QSPI in memory-mapped mode and
// the jump to the XIP app. Everything before the jump uses QSPI in indirect mode.
#include "boot.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "bind.h"
#include "bind_key.h"
#include "board.h"
#include "flashmap.h"
#include "gpio.h"
#include "install.h"
#include "main.h"
#include "ota_image.h"
#include "qspi_flash.h"
#include "quadspi.h"
#include "spi.h"
#include "spi_nor.h"
#include "usart.h"

#define APP_VECTORS (QSPI_XIP_BASE + IMG_HDR_SIZE)  // the body starts with the vector table (§11.1)
#define RELOAD_MS 5000u                             // Reload held this long restores the factory image

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

static bool reload_pressed(void)
{
    return HAL_GPIO_ReadPin(RELOAD_GPIO_Port, RELOAD_Pin) == GPIO_PIN_RESET;  // active low (R137 pull-up)
}

// Waits while Reload stays pressed, kicking every `kick_ms` (each kick also toggles LED WORK).
// True when it stayed pressed for RELOAD_MS; false as soon as it is released.
static bool reload_held(uint32_t kick_ms)
{
    uint32_t t0 = HAL_GetTick(), last = t0;
    while (reload_pressed()) {
        if (HAL_GetTick() - t0 >= RELOAD_MS)
            return true;
        if (HAL_GetTick() - last >= kick_ms) {
            board_kick();
            last = HAL_GetTick();
        }
        HAL_Delay(10);
    }
    return false;
}

static void wait_release(void)
{
    while (reload_pressed()) {
        board_kick();
        HAL_Delay(100);
    }
}

// Halt (spec §11.5): a board that fails the anti-clone check. LED WORK blinks fast, both
// watchdogs stay fed; Reload does not help. Only reprogramming over SWD does.
static void __attribute__((noreturn)) halt(const char *why, int code)
{
    log_line("boot: %s (%d), halted\r\n", why, code);
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

static const char *why_text(install_why_t w)
{
    switch (w) {
    case WHY_NO_IMAGE:
        return "no valid image in QSPI";
    case WHY_COPY:
        return "copy into QSPI failed";
    case WHY_FACTORY_BAD:
        return "factory image invalid";
    case WHY_STAGING:
        return "staging cannot be erased";
    default:
        return "?";
    }
}

// Stands in for a SPI4 that failed its init (spec §6.2): never used, even if the bus answers later.
static int dead_read(nor_t *n, uint32_t off, void *buf, uint32_t len) { return NOR_EIO; }
static int dead_erase(nor_t *n, uint32_t off) { return NOR_EIO; }
static int dead_program(nor_t *n, uint32_t off, const void *buf, uint32_t len) { return NOR_EIO; }
static nor_t s_dead_spi = {dead_read, dead_erase, dead_program, 0};

static install_result_t run_install(install_t *in, bool reload)
{
    in->reload = reload;
    install_result_t r = install_run(in);
    log_line("boot: install%s%s%s%s%s\r\n", reload ? " (reload)" : "", in->did & DID_SEED ? " seeded-factory" : "",
             in->did & DID_SEED_FAIL ? " SEED-FAILED" : "", in->did & DID_INSTALL ? " installed-staging" : "",
             in->did & DID_RESTORE ? " restored-factory" : "");
    return r;
}

static void __attribute__((noreturn)) start_app(void)
{
    int r = qf_memory_mapped(&hqspi);
    if (r)
        halt("qspi memory-mapped failed", r);
    const volatile uint32_t *v = (const volatile uint32_t *)APP_VECTORS;
    uint32_t msp = v[0], pc = v[1];  // checked by img_check inside install_run
    log_line("boot: jump msp %08lx pc %08lx\r\n", (unsigned long)msp, (unsigned long)pc);
    while (!(huart7.Instance->ISR & USART_ISR_TC)) {
    }
    board_kick();
    jump(msp, pc);
}

void boot_main(void)
{
    board_kick();
    board_latch_zero();
    DBGMCU->APB4FZ1 |= DBGMCU_APB4FZ1_DBG_IWDG1;  // a core halted by the debugger does not trip IWDG1
    log_line("\r\nSBIOT-H750 boot " BOOT_VERSION " rsr=%08lx sysclk=%lu\r\n", (unsigned long)RCC->RSR,
             (unsigned long)HAL_RCC_GetSysClockFreq());
    if (BIND_DEV_KEY)
        log_line("bind: DEV KEY\r\n");
    int r = qf_init(&hqspi, board_kick);
    log_line("boot: qspi id %06lx\r\n", (unsigned long)qf_read_jedec(&hqspi));
    if (r)
        halt("qspi init failed", r);
    static qf_nor_t q;
    static spi_nor_t s;
    qf_nor_init(&q, &hqspi, board_kick);
    r = spi_nor_init(&s, &hspi4, board_kick);
    log_line("boot: spi4 id %06lx%s\r\n", (unsigned long)spi_nor_jedec(&s), r ? " FAILED" : "");
    // A failed SPI4 is not fatal, and not used: nothing is installed, seeded or restored.
    nor_t *spi = r ? &s_dead_spi : &s.nor;

    uint8_t uid[BIND_UID_LEN];
    memcpy(uid, (const void *)UID_BASE, sizeof uid);
    bind_result_t b = bind_check(&q.nor, BIND_PUBKEY, uid, board_kick);
    static const char *const bind_text[] = {"ok", "record unreadable", "not provisioned", "record corrupt",
                                            "CLONE: application erased"};
    log_line("bind: %s\r\n", bind_text[b]);
    if (b != BIND_OK)
        halt("anti-clone check failed", b);

    // Reload held from power-up: the factory image (spec §11.2 step 1); LED WORK blinks slowly.
    bool reload = reload_pressed() && reload_held(500);
    install_t in = {.qspi = &q.nor, .spi = spi, .kick = board_kick};
    if (run_install(&in, reload) == INSTALL_JUMP) {
        wait_release();
        start_app();
    }
    // Halt with nothing runnable (spec §11.2): LED WORK blinks fast; Reload held 5 s restores.
    log_line("boot: %s, halted; hold Reload 5 s to restore the factory image\r\n", why_text(in.why));
    for (;;) {
        board_kick();
        HAL_Delay(100);
        if (reload_pressed() && reload_held(100) && run_install(&in, true) == INSTALL_JUMP) {
            wait_release();
            start_app();
        }
    }
}
