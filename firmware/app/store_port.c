// components/store's platform (store_port.h) on FreeRTOS: a recursive mutex around FlashDB and
// the text cache, the clocks, and the log.
#include "store_port.h"
#include <stdarg.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "blog.h"
#include "now_us.h"
#include "sysmgr.h"
#include "store.h"
#include "wdog.h"

static StaticSemaphore_t s_mem;
static SemaphoreHandle_t s_lock;

void store_port_lock(void)
{
    if (!s_lock)  // first used by init (store_init), before any other task touches the store
        s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_mem);
    // In 1 s slices: while the holder makes progress (a factory reset or a GC erasing: store_progress) this wait
    // is progress too and beats the waiter's heartbeat; a holder that stopped moving beats nothing, so the
    // supervisor still restarts a stuck store (review 3 P2-3: disc or web waiting through a 40 s factory reset).
    uint32_t seen = store_progress();
    while (xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE)
        if (store_wait_progressed(&seen))
            wdog_hb_beat_self();
}

void store_port_unlock(void) { xSemaphoreGiveRecursive(s_lock); }

int64_t store_port_wall_ms(void) { return sysmgr_utc_ms(); }  // 0 while the clock is not set (spec §10.4)

uint32_t store_port_uptime_ms(void) { return (uint32_t)(now_us() / 1000u); }

void store_port_log(const char *fmt, ...)
{
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    blog("store: %s\r\n", line);
}

void store_port_restart(void) { sysmgr_restart_later(1500); }

void store_port_progress(void) { wdog_hb_beat_self(); }  // nothing for a task that is not supervised
