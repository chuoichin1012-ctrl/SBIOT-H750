Code review (read-only) of the one-clock change you approved with changes (stm32/docs/review/one-clock-design.md): branch schedule-clock, commit 906a4b3 (`git diff main..906a4b3 -- stm32`).

- clock.c: clock_device_ms(real_ms, uptime_ms, tz) = real, else (CLOCK_BOOT_EPOCH − tz·3600)·1000 + uptime_ms.
- sysmgr: sysmgr_now_ms() = the device clock; sysmgr_set_zone(system) reads the zone, called in app_main right after `system` is read, before iomgr_start (your change); sysmgr_start keeps NTP only; sysmgr_display_time formats sysmgr_now_ms(); sysmgr_time_str removed (unused); sysmgr_utc_ms stays real-only (TLS, cert_check, the log's wall time).
- iomgr: io_core_timers(c, sysmgr_now_ms()/1000, sysmgr_timezone(), true, now); its own zone copy removed (one source of the zone too).
- edge_rt: edge_sys_value and edge_schedule_due take sysmgr_now_ms().
- The boot log line without NTP; UI help texts (Report Scheduled, IO timers, types.ts); spec §10.4 rewritten (incl. your clarifications: first minute, the switch to real time, sys_time UTC).
- Tests: test_clock test_device_clock (RED first: clock_device_ms missing, then GREEN); test_io_core test_timers_device_clock and test_edge_core test_device_clock (composition of pure pieces with the device clock; they passed at once since io_core/edge_core did not change — the behaviour change is the glue in iomgr/edge_rt). App build, check_elf ok; npm test:unit 25/25, tsc ok.
- Board (web OTA; ntp 0, timezone 7): /api/status time 2020-01-01 00:00:11 at uptime 11, clockSet false.

Reply with findings ranked (Critical / Important / Minor), then a verdict: APPROVED or CHANGES NEEDED.
