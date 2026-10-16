#include "wdog.h"
#include <stdbool.h>
#include "FreeRTOS.h"
#include "board.h"
#include "now_us.h"
#include "pace.h"
#include "task.h"

static volatile bool s_stop, s_halt;
static volatile int s_busy;
static volatile uint32_t s_max_gap_us, s_wdi_max_gap_us;

void wdog_stop(void)
{
    s_stop = true;
}

void wdog_set_busy(bool on)
{
    taskENTER_CRITICAL();
    s_busy += on ? 1 : (s_busy > 0 ? -1 : 0);
    taskEXIT_CRITICAL();
}

void wdog_set_halt(void)
{
    s_halt = true;
}

uint32_t wdog_max_gap_us(void)
{
    return s_max_gap_us;
}

uint32_t wdog_wdi_max_gap_us(void)
{
    return s_wdi_max_gap_us;
}

void wdog_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    uint64_t last = now_us(), last_wdi = last;
    uint32_t paced = (uint32_t)(last / 1000u);
    for (;;) {
        uint64_t t = now_us();  // also keeps the 64-bit extension of TIM2 current
        if (!s_stop) {
            board_iwdg_reload();
            if (t - last > s_max_gap_us)
                s_max_gap_us = (uint32_t)(t - last);
            last = t;
            uint32_t half = s_halt ? PACE_HALT_MS : s_busy > 0 ? PACE_BUSY_MS : PACE_RUN_MS;
            if (pace_due((uint32_t)(t / 1000u), &paced, half)) {
                board_wdi_toggle();
                if (t - last_wdi > s_wdi_max_gap_us)
                    s_wdi_max_gap_us = (uint32_t)(t - last_wdi);
                last_wdi = t;
            }
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(50));
    }
}
