// Plan 02 app (spec §6.1, §6.3): board_early, then the wdog and init tasks and the scheduler.
// init reports the boot, checks the anti-clone record (spec §11.5) and then serves as the UART7
// bench console: 'i' info, 'r' software reset, 'w' stop the watchdog kicks, 'c' CRC-32 of the
// image, 'o' the OTA images, 'u' upload an image into staging. Later plans replace it.
#include "app_main.h"
#include "bench_ota.h"
#include <stdbool.h>
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

static void init_task(void *arg)
{
    report_boot();
    if (!app_bind_ok()) {  // spec §11.5: no service starts on a board this image is not bound to
        blog("app: anti-clone record INVALID, nothing started\r\n");
        for (;;)
            vTaskDelay(pdMS_TO_TICKS(1000));
    }
    bench_ota_init();
    info();
    for (;;) {
        uint8_t c;
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
        case 'u':
            bench_ota_upload();
            break;
        default:
            break;
        }
    }
}

void app_main(void)
{
    board_latch_zero();  // MX_GPIO_Init (app.ioc) left the latch outputs disabled
    board_kick();
    blog_init();
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
