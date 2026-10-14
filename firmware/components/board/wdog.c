#include "wdog.h"
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "now_us.h"

static volatile bool s_stop;
static volatile uint32_t s_max_gap_us;

void wdog_stop(void)
{
    s_stop = true;
}

uint32_t wdog_max_gap_us(void)
{
    return s_max_gap_us;
}

void wdog_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    uint64_t last = now_us();
    for (;;) {
        uint64_t t = now_us();  // also keeps the 64-bit extension of TIM2 current
        if (!s_stop) {
            board_kick();
            if (t - last > s_max_gap_us)
                s_max_gap_us = (uint32_t)(t - last);
            last = t;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(200));
    }
}
