Replaces the schedule-clock design you approved (stm32/docs/review/schedule-clock-design.md): the user asks for ONE time source for the whole device, and decided (2026-10-17): sys_time/sys_unix_time carry the gateway's clock while there is no real time; the 6 IO timers run on that same clock too.

Design (read-only review; stm32/firmware, stm32/web, spec §10.4):
- clock.c (pure, test_clock): `int64_t clock_device_ms(int64_t real_ms, uint64_t uptime_ms, int tz_hours)`: real_ms > 0 → real_ms; else (CLOCK_BOOT_EPOCH − tz·3600)·1000 + uptime_ms. So the local time of the device clock reads 2020-01-01 00:00:00 plus the uptime, exactly what the display shows today (Plan 08b), and every consumer applies the zone the same way.
- sysmgr: `int64_t sysmgr_now_ms(void)` = clock_device_ms(real, uptime, tz): THE device clock. `sysmgr_clock_set()` stays (is it real). `sysmgr_utc_ms()` stays, documented as "the device clock when it is real, else 0", and is used only where a fake date would be wrong: TLS (tls_port time, tlsconn date skip) and web.c's certificate check (cert_check). sysmgr_display_time() formats sysmgr_now_ms() in the zone.
- Consumers moved to sysmgr_now_ms: Edge sys_time/sys_unix_time (edge_sys_value), Edge scheduled reports (edge_schedule_due), IO timers (io_core_timers(c, now_s, tz, true, ...): its existing guard — nothing fires after a clock that went back or jumped past IO_MAX_CATCH_UP_S — covers the switch from 2020 to real time).
- The log keeps "[boot N +hh:mm:ss]" when the entry has no real time: the log spans restarts, the device clock restarts at 2020 each boot, and boot + uptime is that same clock with its boot number. store_port_wall_ms stays real-only (0 = no real time).
- /api/status `time` and `clockSet` unchanged (time already = the device clock).
- UI help texts: Report "Scheduled": "Report on the gateway's clock"; IO timers: "Timers run on the gateway clock: network time once set, else the time since start (2020-01-01 00:00 at power-on). Turn on NTP for real times."
- Spec §10.4 rewritten; clock.h's "display only" note changed.
- Tests: test_clock (no real → local 2020-01-01 00:00:00 + uptime in any zone; real passes through); test_edge_core (schedule on the device clock: first minute, cadence, daily at hh:mm after boot, switch to a matching real minute — your r1 clarifications; sys_time text on the device clock); test_io_core (a timer at 08:00 fires at 8 h of uptime on the device clock; the jump to real time fires nothing).

Reply APPROVED or CHANGES NEEDED with reasons.
