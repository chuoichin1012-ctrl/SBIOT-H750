# Code review: branch `ntp-defaults` (three requests from the user)

Please review `git diff main..ntp-defaults -- stm32`.

1. **NTP was slow** (the user: minutes before the clock was set).
   - **Measured on board 2** with lwIP's SNTP/DNS/ARP debug:
     - `sntp_init` runs at 0.3 s, before the link and DHCP; its first request fails ("Invalid server address": no DNS server yet);
     - lwIP's schedule then waits 15 s plus a 15 s retry, so the request leaves at about 32.7 s and is answered at once;
     - when one request was lost: 32.7 + 15 (receive timeout) + 30 (retry) ≈ 77.6 s, as the user saw.
   - **Fix:**
     - `sysmgr_ntp_kick()` (`sysmgr.c`) is called by `netmgr.c route_check` when the default route moves to a link, after `dns_apply`. With NTP on and the clock not set, it calls `sntp_stop(); sntp_init();`, so a request leaves at once.
     - `lwipopts.h`: `SNTP_STARTUP_DELAY 0`, `SNTP_RECV_TIMEOUT 3000`, `SNTP_RETRY_TIMEOUT 15000` (RFC 4330 §10's minimum), `SNTP_RETRY_TIMEOUT_MAX 60000`. Updates stay hourly.
   - **On board 2:** the clock is set 0.1 s after the route in 3 of 5 boots 75 s apart. In the other 2, the single pool server that the PC's ICS DNS returns did not answer; it rate-limits, and dropped `w32tm` samples 2 s apart from the PC too.
   - A 5 s retry was tried first and dropped for RFC 4330.
2. **Factory network defaults** (`cfgmodel.c DEF_NETWORK`): `priority` 0 (Ethernet only); it was already static 192.168.0.7. `test_cfgmodel` pins the defaults. Bench comments are updated.
3. **`sys_time` / `sys_unix_time` in the gateway's time zone:**
   - `edge_sys_value(field, utc_ms, tz_hours, …)`: local text, and Unix seconds plus `tz·3600`;
   - `edge_rt` passes `sysmgr_timezone()`;
   - covered by `test_edge_core` (UTC+0, +7 and −5, and power-on with no real time);
   - the UI help, the spec and the `edge_check` expectations are updated.

Findings as P1 / P2 / minor with file:line and a failure scenario. End with **APPROVED** or **CHANGES NEEDED**.
