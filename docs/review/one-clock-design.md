**CHANGES NEEDED** — initialize the clock’s timezone before starting its consumers.

Currently, `iomgr_start()` runs before `sysmgr_start()` ([app_main.c:236](/home/linh/projects/SBIOT-Gateway/stm32/firmware/app/app_main.c:236)). IO already holds the configured timezone, while sysmgr still has timezone 0. With the proposed fallback, an IO task running during initialization could therefore evaluate timers at the wrong local hour.

Set sysmgr’s timezone before starting IO and Edge; SNTP startup can remain later. Add a startup-order check for a nonzero timezone.

Otherwise the design is sound:

- The IO jump guard suppresses catch-up actions when switching from 2020 to real time.
- Preserve `sys_time` as **UTC text** and `sys_unix_time` as Unix seconds. For UTC+7 at boot, the display is `2020-01-01 00:00:00`, while `sys_time` is `2019-12-31 17:00:00`—different representations of the same clock. Test this explicitly.
- Keeping TLS, certificate checks and persistent log timestamps real-only is appropriate.