// System services (spec §4, §6.3, §10.4): the clock (SNTP or POST /api/time, the timezone, "not set"),
// restarts, the serial number and the uptime. The clock functions work from boot on; sysmgr_start runs
// last in init.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

/** Reads system.timezone once (spec §9.1): init calls it before anything reads the clock (IO, Edge; Codex). */
void sysmgr_set_zone(const cJSON *system);
/** Reads ntp and ntpServer from `system` once (spec §9.1), starts SNTP when ntp is 1. */
void sysmgr_start(const cJSON *system);
bool sysmgr_clock_set(void);
/** system.timezone in whole hours (-12..12), as read by sysmgr_set_zone. */
int sysmgr_timezone(void);
/** THE device clock, in Unix ms (clock_device_ms): the real time once set, else 2020-01-01 00:00:00 local plus
 *  the uptime. Edge (sys_time, scheduled reports) and the IO timers read it (the user, 2026-10-17: one source). */
int64_t sysmgr_now_ms(void);
/** The device clock when it is real, else 0: only for what a 2020 date would break (TLS and certificate dates)
 *  and for the log's wall time. */
int64_t sysmgr_utc_ms(void);
/** What the UI and the console show: the device clock as local time (out holds 20 bytes). */
void sysmgr_display_time(char out[20]);
/** A Unix time in ms as local "YYYY-MM-DD hh:mm:ss" in the configured zone (out holds 20 bytes). */
void sysmgr_format_wall(int64_t utc_ms, char out[20]);
/** POST /api/time: a local time in the configured zone; 0, or -1 when it is not a valid time. */
int sysmgr_set_local(const char *text);
/** lwIP's SNTP (SNTP_SET_SYSTEM_TIME_US), in the lwIP core. */
void sysmgr_sntp_set(uint32_t sec, uint32_t us);
/** The default route moved to a link, its DNS set (netmgr, in the lwIP core): with NTP on and no real time yet, SNTP
 *  asks now instead of waiting out a retry that began before the network (the user, 2026-10-17). */
void sysmgr_ntp_kick(void);
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

/** The SN from the binding record (Plan 11a); init sets it before any service starts. */
void sysmgr_set_serial(const char *sn);
const char *sysmgr_serial(void);
