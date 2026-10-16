#include "wdog.h"
#include <stdbool.h>
#include "FreeRTOS.h"
#include "blog.h"
#include "board.h"
#include "hb.h"
#include "stm32h7xx.h"
#include "now_us.h"
#include "pace.h"
#include "task.h"

static volatile bool s_stop, s_halt;
static volatile int s_busy;
static volatile uint32_t s_max_gap_us, s_wdi_max_gap_us, s_hb_missing;
// The reason, alone in a 32-byte cache line of SRAM4 that no startup clears (Codex plan r5: cleaned to RAM
// after every write, invalidated before the read at the boot).
static union {
    hb_reason_t r;
    uint8_t line[32];
} s_reason __attribute__((section(".noinit"), aligned(32)));
_Static_assert(sizeof(hb_reason_t) <= 32, "the reason fits its cache line");

static uint32_t now_ms(void) { return (uint32_t)(now_us() / 1000u); }

static void reason_flush(void)
{
    SCB_CleanDCache_by_Addr((uint32_t *)s_reason.line, sizeof s_reason.line);
    __DSB();
}

int wdog_hb_add(const char *name, uint32_t timeout_ms)
{
    taskENTER_CRITICAL();
    int id = hb_add(name, timeout_ms, now_ms());
    taskEXIT_CRITICAL();
    if (id < 0) {
        s_hb_missing++;
        blog("hb: no slot for %s: it is not supervised\r\n", name ? name : "?");
    }
    return id;
}

void wdog_hb_beat(int id) { hb_beat(id, now_ms()); }

uint32_t wdog_hb_missing(void) { return s_hb_missing; }

bool wdog_reason_take(char name[16], uint32_t *late_s)
{
    SCB_InvalidateDCache_by_Addr((uint32_t *)s_reason.line, sizeof s_reason.line);
    bool got = hb_reason_get(&s_reason.r, name, late_s);
    hb_reason_clear(&s_reason.r);
    reason_flush();
    return got;
}

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
    uint32_t paced = (uint32_t)(last / 1000u), checks = 0;
    for (;;) {
        uint64_t t = now_us();  // also keeps the 64-bit extension of TIM2 current
        if (!s_stop) {
            board_iwdg_reload();
            if (t - last > s_max_gap_us)
                s_max_gap_us = (uint32_t)(t - last);
            last = t;
            if (++checks >= 20) {  // every second: the supervised tasks
                checks = 0;
                uint32_t now = (uint32_t)(t / 1000u);
                int late = hb_expired(now);
                if (late >= 0) {  // no blog, no lock: the reason in .noinit, then IWDG1 resets (Codex plan r4)
                    hb_reason_set(&s_reason.r, hb_name(late), hb_late_ms(late, now) / 1000u);
                    reason_flush();
                    s_stop = true;
                    vTaskDelayUntil(&wake, pdMS_TO_TICKS(50));
                    continue;
                }
            }
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
