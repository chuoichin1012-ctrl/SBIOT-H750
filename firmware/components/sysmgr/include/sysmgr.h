// System services (spec §4, §6.3, §10.4): the clock (SNTP or POST /api/time, the timezone, "not set"),
// restarts, the serial number and the uptime. The clock functions work from boot on; sysmgr_start runs
// last in init.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

/** Reads ntp, ntpServer and timezone from `system` once (spec §9.1), starts SNTP when ntp is 1. */
void sysmgr_start(const cJSON *system);
bool sysmgr_clock_set(void);
/** system.timezone in whole hours (-12..12), as read by sysmgr_start. */
int sysmgr_timezone(void);
/** Unix time in ms, 0 while the clock is not set. */
int64_t sysmgr_utc_ms(void);
/** Local "YYYY-MM-DD hh:mm:ss", or "" while the clock is not set (out holds 20 bytes). */
void sysmgr_time_str(char out[20]);
/** What the UI and the console show: the local time, or 2020-01-01 00:00:00 plus the uptime while the
 * clock is not set (Plan 08b, display only; out holds 20 bytes). */
void sysmgr_display_time(char out[20]);
/** A Unix time in ms as local "YYYY-MM-DD hh:mm:ss" in the configured zone (out holds 20 bytes). */
void sysmgr_format_wall(int64_t utc_ms, char out[20]);
/** POST /api/time: a local time in the configured zone; 0, or -1 when it is not a valid time. */
int sysmgr_set_local(const char *text);
/** lwIP's SNTP (SNTP_SET_SYSTEM_TIME_US), in the lwIP core. */
void sysmgr_sntp_set(uint32_t sec, uint32_t us);
/** Resets the MCU after delay_ms (the answer to the request gets out first). */
void sysmgr_restart_later(uint32_t delay_ms);
/** The one restart: true for the first caller, who resets the board itself; every later restart is a no-op. */
bool sysmgr_restart_claim(void);
/**
 * A heartbeat or registration payload (the ESP32's sys_payload): content 0 the custom text (as it is,
 * or pairs of hex digits with `hex`), 1 IMEI, 2 serial number, 3 ICCID, 4 MAC. Its length; 0 when
 * there is nothing to send (IMEI and ICCID until the modem, Plan 10).
 */
size_t sysmgr_payload(int content, const char *custom, bool hex, uint8_t *out, size_t max);
uint32_t sysmgr_uptime_s(void);
/** The chip UID as 24 hex digits. */
const char *sysmgr_serial(void);
