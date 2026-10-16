#include "services.h"
#include <stdio.h>
#include "FreeRTOS.h"
#include "bridge.h"
#include "bridge_core.h"
#include "cfgmodel.h"
#include "edge_rt.h"
#include "now_us.h"
#include "serial.h"
#include "store.h"
#include "syslog.h"
#include "sysmgr.h"
#include "task.h"

#define IDLE_STACK_WORDS 512  // 2 KB (spec §6.1)
#define IDLE_PRIO 2

static volatile uint64_t s_net_us;  // 64 bits shared between tasks: under a critical section

void svc_net_activity(void)
{
    uint64_t now = now_us();
    taskENTER_CRITICAL();
    s_net_us = now;
    taskEXIT_CRITICAL();
}

static uint64_t last_activity(void)
{
    taskENTER_CRITICAL();
    uint64_t t = s_net_us;
    taskEXIT_CRITICAL();
    return t;
}

// system.idleRestart: a restart when no data moved to or from the network for that many seconds.
static void idle_task(void *arg)
{
    uint64_t limit_us = (uint64_t)(uintptr_t)arg * 1000000u;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint64_t last = last_activity();  // first: a bridge may stamp it in between (Codex code review r1)
        if (idle_due(now_us(), last, limit_us)) {
            SLOG_W("svc", "no data flowed for %lu s: restarting", (unsigned long)(limit_us / 1000000u));
            sysmgr_restart_later(500);
            vTaskDelete(NULL);
        }
    }
}

void services_start(const cJSON *sys)
{
    svc_net_activity();
    for (int port = 1; port <= SERIAL_PORTS; port++) {
        char name[8];
        snprintf(name, sizeof name, "port%d", port);
        cJSON *pc = store_get(name);
        if (!pc)
            pc = cfg_default(store_env(), name);
        bridge_start(port, pc, sys);
        cJSON_Delete(pc);
    }
    cJSON *edge = store_get("edge");  // Plan 08c: after the bridges (it hooks into them), before the web
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(edge, "enabled")))
        edge_start();
    cJSON_Delete(edge);
    const cJSON *idle = cJSON_GetObjectItemCaseSensitive(sys, "idleRestart");
    if (cJSON_IsNumber(idle) && idle->valueint > 0) {
        static StaticTask_t tcb;
        static StackType_t stack[IDLE_STACK_WORDS];
        xTaskCreateStatic(idle_task, "idle", IDLE_STACK_WORDS, (void *)(uintptr_t)idle->valueint, IDLE_PRIO, stack, &tcb);
        SLOG_I("svc", "idleRestart: %d s without network data", idle->valueint);
    }
}
