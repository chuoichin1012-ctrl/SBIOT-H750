#include "sysmgr.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "blog.h"
#include "clock.h"
#include "lwip/apps/sntp.h"
#include "lwip/tcpip.h"
#include "lte.h"
#include "netcfg.h"
#include "netmgr.h"
#include "now_us.h"
#include "serial_frame.h"
#include "stm32h7xx.h"
#include "syslog.h"
#include "task.h"

#define RESTART_STACK_WORDS 768  // 3 KB (spec §6.1)

static int64_t s_offset_us;  // UTC in us minus now_us(); 0: not set. In a critical section (64 bits).
static int s_tz;             // hours, from system.timezone at boot
static char s_ntp_server[129];  // lwIP keeps the pointer: lives for the whole run
static bool s_ntp_on, s_ntp_started;  // both in the lwIP core: NTP configured; SNTP started (once, never restarted)
static char s_serial[13];  // SB + 10 digits, from the binding record (Plan 11a)

static int64_t offset(void)
{
    taskENTER_CRITICAL();
    int64_t o = s_offset_us;
    taskEXIT_CRITICAL();
    return o;
}

static void set_utc_us(int64_t utc_us)
{
    int64_t o = utc_us - (int64_t)now_us();
    taskENTER_CRITICAL();
    s_offset_us = o ? o : 1;
    taskEXIT_CRITICAL();
}

bool sysmgr_clock_set(void)
{
    return offset() != 0;
}

int64_t sysmgr_utc_ms(void)
{
    int64_t o = offset();
    return o ? (o + (int64_t)now_us()) / 1000 : 0;
}

int sysmgr_timezone(void) { return s_tz; }

int64_t sysmgr_now_ms(void) { return clock_device_ms(sysmgr_utc_ms(), (uint64_t)now_us() / 1000u, s_tz); }

void sysmgr_display_time(char out[20])
{
    clock_format(sysmgr_now_ms() / 1000, s_tz, out);
}

void sysmgr_format_wall(int64_t utc_ms, char out[20])
{
    clock_format(utc_ms / 1000, s_tz, out);
}

int sysmgr_set_local(const char *text)
{
    civil_t c;
    if (!clock_parse(text, &c))
        return -1;
    set_utc_us(clock_local_to_utc(&c, s_tz) * 1000000);
    SLOG_I("sys", "clock set to %s (UTC%+d)", text, s_tz);
    return 0;
}

void sysmgr_sntp_set(uint32_t sec, uint32_t us)
{
    if (!clock_plausible((int64_t)sec))
        return;  // a server answering 1970 does not set the clock
    bool first = !sysmgr_clock_set();
    set_utc_us((int64_t)sec * 1000000 + us);
    if (first)
        SLOG_I("sys", "clock set by NTP");
}

void sysmgr_set_zone(const cJSON *system)
{
    const cJSON *tz = cJSON_GetObjectItemCaseSensitive(system, "timezone");
    s_tz = cJSON_IsNumber(tz) && tz->valueint >= -12 && tz->valueint <= 12 ? tz->valueint : 0;
}

void sysmgr_start(const cJSON *system)
{
    const cJSON *ntp = cJSON_GetObjectItemCaseSensitive(system, "ntp");
    const cJSON *srv = cJSON_GetObjectItemCaseSensitive(system, "ntpServer");
    if (cJSON_IsNumber(ntp) && ntp->valueint == 1 && cJSON_IsString(srv) && srv->valuestring[0]) {
        snprintf(s_ntp_server, sizeof s_ntp_server, "%s", srv->valuestring);
        LOCK_TCPIP_CORE();  // the SNTP app is part of the lwIP core
        s_ntp_on = true;
        if (strcmp(netmgr_route_name(), "NONE") != 0)
            sysmgr_ntp_kick();  // a route came up already: start now
        UNLOCK_TCPIP_CORE();
        blog("sys: NTP from %s, UTC%+d\r\n", s_ntp_server, s_tz);
    } else {
        blog("sys: no NTP, UTC%+d; the device clock runs from 2020-01-01 00:00 until POST /api/time\r\n", s_tz);
    }
}

// SNTP starts once, when the first route is up (its DNS set): started at boot it asked before the network and its
// retries made the first clock take a minute or more (the user, 2026-10-17). Never restarted: a restart would leave a
// pending DNS lookup's callback alive beside the new one, and two requests would go at once (Codex review). Later
// route changes are left to its retries (at most 60 s apart) and its hourly update.
void sysmgr_ntp_kick(void)
{
    if (!s_ntp_on || s_ntp_started)
        return;
    s_ntp_started = true;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, s_ntp_server);
    sntp_init();  // SNTP_STARTUP_DELAY 0: the request leaves now
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)arg));
    if (!lte_shutdown(60000))  // the modem powered down first (spec §7.2, Plan 10 T5)
        blog("sys: the modem did not confirm POWERED DOWN\r\n");
    blog("sys: restarting\r\n");
    vTaskDelay(pdMS_TO_TICKS(20));  // the line out of UART7
    NVIC_SystemReset();
}

static bool s_restart_claimed;

bool sysmgr_restart_claim(void)
{
    taskENTER_CRITICAL();
    bool again = s_restart_claimed;
    s_restart_claimed = true;
    taskEXIT_CRITICAL();
    return !again;
}

void sysmgr_restart_later(uint32_t delay_ms)
{
    static StaticTask_t tcb;
    static StackType_t stack[RESTART_STACK_WORDS];
    if (sysmgr_restart_claim())
        xTaskCreateStatic(restart_task, "restart", RESTART_STACK_WORDS, (void *)(uintptr_t)delay_ms, 10, stack, &tcb);
}

size_t sysmgr_payload(int content, const char *custom, bool hex, uint8_t *out, size_t max)
{
    char text[32] = "";  // the serial number: 24 hex digits
    if (content == 0)
        return sf_payload_custom(custom, hex, out, max);
    if (content == 2) {
        snprintf(text, sizeof text, "%s", sysmgr_serial());
    } else if (content == 4) {
        net_status_t ns;
        netmgr_status(&ns);
        snprintf(text, sizeof text, "%02x:%02x:%02x:%02x:%02x:%02x", ns.mac[0], ns.mac[1], ns.mac[2], ns.mac[3], ns.mac[4], ns.mac[5]);
    } else if (content == 1 || content == 3) {  // empty until the modem has answered, as the ESP32 without one
        lte_info_t li;
        lte_info(&li);
        snprintf(text, sizeof text, "%s", content == 1 ? li.imei : li.iccid);
    }
    size_t n = strlen(text) < max ? strlen(text) : max;
    memcpy(out, text, n);
    return n;
}

uint32_t sysmgr_uptime_s(void)
{
    return (uint32_t)(now_us() / 1000000u);
}

void sysmgr_set_serial(const char *sn)
{
    snprintf(s_serial, sizeof s_serial, "%s", sn);
}

const char *sysmgr_serial(void)
{
    return s_serial;  // from the binding record (Plan 11a), set before any service starts
}
