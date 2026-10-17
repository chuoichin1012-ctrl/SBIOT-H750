The user (2026-10-17): Edge's scheduled reports must run on the gateway's own clock while there is no real time, because NTP can be on and still never get a time (no Internet). Today edge_rpt calls edge_schedule_due(sysmgr_utc_ms()/1000, tz, ...) and it is never due while the clock is unset (spec §10.4: "report theo schedule ... chưa kích hoạt").

Short design (please approve or correct, read-only; stm32/firmware, stm32/web, spec §10.4):
- New pure function in edge_core: `int64_t edge_schedule_time(int64_t utc_s, uint32_t uptime_s, int tz_hours, int *tz_out)`: utc_s > 0 → utc_s and tz_hours; otherwise CLOCK_BOOT_EPOCH + uptime_s and tz 0 — the very clock the UI and console display (Plan 08b, "2020-01-01 00:00:00 plus the uptime, no zone").
- edge_rt rpt_task passes it to edge_schedule_due. Hourly/15 min/every minute keep their cadence counted from boot; daily hh:mm fires at hh:mm of that clock (hh:mm after boot) until real time exists. When a real clock appears the schedule switches to it; `last` is minutes since the epoch, so the same minute never fires twice.
- Unchanged: sys_time/sys_unix_time stay empty without a real clock; the 6 IO timers; TLS and logs still treat "no time" as now.
- clock.h's "Display only: nothing that depends on the clock ever reads it" becomes "...nothing but Edge's scheduled reports"; spec §10.4 updated; the Report page's Scheduled help says "Report on the gateway's clock (network time once set, else the time since start)".
- Tests: test_edge_core: no clock → 2020 + uptime, zone 0; a real clock passes through with its zone; every-minute fires each minute of uptime; daily 08:00 fires at uptime 8 h; switching to a real clock does not fire the same minute twice.

Reply APPROVED or CHANGES NEEDED with reasons.
