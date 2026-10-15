// Plan 02 app (spec §6.1, §6.3): board_early, then the wdog and init tasks and the scheduler.
// init reports the boot, checks the anti-clone record (spec §11.5) and then serves as the UART7
// bench console: 'i' info, 'r' software reset, 'w' stop the watchdog kicks, 'c' CRC-32 of the
// image, 'o' the OTA images, 'u' upload an image into staging and reset ('U' without the reset).
// Later plans replace it.
#include "app_main.h"
#include "bench_ota.h"
#include "heap_newlib.h"
#include "flash_spi4.h"
#include "store.h"
#include "syslog.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "blog.h"
#include "board.h"
#include "crc32.h"
#include "main.h"
#include "now_us.h"
#include "startup_app.h"
#include "usart.h"
#include "wdog.h"

#define WDOG_STACK_WORDS 512   // 2 KB (spec §6.1)
#define INIT_STACK_WORDS 1536  // 6 KB

static StaticTask_t s_wdog_tcb, s_init_tcb;
static StackType_t s_wdog_stack[WDOG_STACK_WORDS], s_init_stack[INIT_STACK_WORDS];

extern const uint8_t __image_start__[], __image_end__[];  // app.ld: the whole QSPI image

static void report_boot(void)
{
    uint32_t rsr = RCC->RSR;
    RCC->RSR |= RCC_RSR_RMVF;  // the next boot reports only its own cause
    blog("\r\nSBIOT-H750 app " APP_VERSION " rsr=%08lx sysclk=%lu\r\n", (unsigned long)rsr,
         (unsigned long)SystemCoreClock);
    blog("app: handoff %s (%02lx), mpu %s\r\n", g_handoff_faults ? "FAULT" : "ok", (unsigned long)g_handoff_faults,
         g_mpu_boot_match ? "regions 0-1 match boot" : "MISMATCH with boot");
    blog("app: itcm vTaskSwitchContext=%p memcpy=%p\r\n", (void *)vTaskSwitchContext, (void *)memcpy);
}

static void info(void)
{
    // newlib-nano printf has no %llu: report milliseconds, which fit 32 bits for 49 days.
    unsigned long ms = (unsigned long)(now_us() / 1000u);
    blog("app: uptime_ms=%lu ticks=%lu wdog_max_gap_us=%lu\r\n", ms, (unsigned long)xTaskGetTickCount(),
         (unsigned long)wdog_max_gap_us());
}

static bool print_log_line(const store_log_meta_t *m, const char *line, size_t len, void *ctx)
{
    (void)ctx;
    blog("app: log boot %lu +%lu ms: %.*s\r\n", (unsigned long)m->boot, (unsigned long)m->uptime_ms, (int)len, line);
    return true;
}

static void init_task(void *arg)
{
    report_boot();
    if (!app_bind_ok()) {  // spec §11.5: no service starts on a board this image is not bound to
        blog("app: anti-clone record INVALID, nothing started\r\n");
        for (;;)
            vTaskDelay(pdMS_TO_TICKS(1000));
    }
    bench_ota_init();
    // The configs are read once, here; a saved config applies after a restart (spec §9.1).
    static const cfg_env_t env = {.uart_count = 2, .do_count = 2, .di_count = 2, .ai_count = 2};
    uint64_t t0 = now_us();
    store_err_t se = store_init(&env, flash_spi4());
    blog("app: store %s in %lu ms, boot %lu, heap free %lu\r\n", se == STORE_OK ? "mounted" : "FAILED",
         (unsigned long)((now_us() - t0) / 1000u), (unsigned long)store_boot_count(), (unsigned long)heap_free_now());
    info();
    for (;;) {
        uint8_t c;
        // Sleep while nothing came in: HAL_UART_Receive waits by spinning, which would starve every
        // task below init's priority (syslog, idle) for as long as the console is quiet.
        if (!(huart7.Instance->ISR & USART_ISR_RXNE_RXFNE)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (HAL_UART_Receive(&huart7, &c, 1, 50) != HAL_OK)
            continue;
        switch (c) {
        case 'i':
            info();
            break;
        case 'r':
            blog("app: software reset\r\n");
            vTaskDelay(pdMS_TO_TICKS(20));
            NVIC_SystemReset();
            break;
        case 'w':
            wdog_stop();
            blog("app: watchdog kicks stopped at %lu ms\r\n", (unsigned long)(now_us() / 1000u));
            break;
        case 'c': {
            size_t len = (size_t)(__image_end__ - __image_start__);
            uint64_t t0 = now_us();
            uint32_t crc = crc32_update(0, __image_start__, len);
            blog("app: image crc32=%08lx len=%lu in %lu us\r\n", (unsigned long)crc, (unsigned long)len,
                 (unsigned long)(now_us() - t0));
            break;
        }
        case 'o':
            bench_ota_report();
            break;
        case 'S': {  // bench: the store's state
            store_edge_pin_t p;
            store_edge_pin(&p);
            store_edge_unpin(&p);
            char *sys = store_get_json("system");
            blog("app: store boot %lu edge gen %lu records %lu heap free %lu min %lu dma errors %u\r\n",
                 (unsigned long)store_boot_count(), (unsigned long)p.gen, (unsigned long)p.count, (unsigned long)heap_free_now(),
                 (unsigned long)heap_free_min(), flash_spi4_dma_errors());
            blog("app: system %.120s\r\n", sys ? sys : "(none)");
            blog("app: log %s, %u appends failed\r\n", store_log_ok() ? "mounted" : "NOT MOUNTED", store_log_failures());
            free(sys);
            break;
        }
        case 'P':  // bench: microseconds of idle since the boot, and the clock: the host takes the load
            blog("app: idle_us=%lu now_us=%lu\r\n", (unsigned long)ulTaskGetIdleRunTimeCounter(), (unsigned long)now_us());
            break;
        case 'G': {  // bench: 1000 saves in a row (about 300 KB: garbage collection in cfg), the slowest reported
            char text[64], err[160];
            uint32_t worst = 0, fails = 0;
            uint64_t idle0 = ulTaskGetIdleRunTimeCounter(), at0 = now_us();
            for (int i = 0; i < 1000; i++) {
                snprintf(text, sizeof text, "{\"hostname\":\"stress-%d\"}", i);
                uint64_t t0 = now_us();
                fails += store_set_text("system", text, strlen(text), err, sizeof err) != STORE_OK;
                uint32_t us = (uint32_t)(now_us() - t0);
                worst = us > worst ? us : worst;
            }
            uint64_t idle = ulTaskGetIdleRunTimeCounter() - idle0, took = now_us() - at0;
            blog("app: stress 1000 saves, %lu failed, slowest %lu us, %lu ms, idle %lu permille\r\n", (unsigned long)fails,
                 (unsigned long)worst, (unsigned long)(took / 1000u), (unsigned long)(idle * 1000u / (took ? took : 1)));
            break;
        }
        case 'W':  // bench: a warning, which syslog keeps in the persistent log
            SLOG_W("bench", "a warning to keep, boot %lu", (unsigned long)store_boot_count());
            break;
        case 'l':  // bench: the persistent log, the last lines
            store_log_iter(print_log_line, NULL);
            break;
        case 'p': {  // bench: save system.hostname = "bench-<uptime ms>" (applies after a restart)
            char text[64], err[160];
            unsigned long ms = (unsigned long)(now_us() / 1000u);
            snprintf(text, sizeof text, "{\"hostname\":\"bench-%lu\"}", ms);
            uint64_t t0 = now_us();
            store_err_t r = store_set_text("system", text, strlen(text), err, sizeof err);
            blog("app: saved bench-%lu: %d in %lu ms %s\r\n", ms, r, (unsigned long)((now_us() - t0) / 1000u), r ? err : "");
            break;
        }
        case 'u':
        case 'U':
            bench_ota_upload(c == 'u');
            break;
        default:
            break;
        }
    }
}

void app_main(void)
{
    heap_init();  // before any allocation (spec §5.5)
    board_latch_zero();  // MX_GPIO_Init (app.ioc) left the latch outputs disabled
    board_kick();
    blog_init();
    syslog_init();  // before the first line worth keeping
    xTaskCreateStatic(wdog_task, "wdog", WDOG_STACK_WORDS, NULL, 31, s_wdog_stack, &s_wdog_tcb);
    xTaskCreateStatic(init_task, "init", INIT_STACK_WORDS, NULL, 7, s_init_stack, &s_init_tcb);
    vTaskStartScheduler();
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    configASSERT(0);
}
