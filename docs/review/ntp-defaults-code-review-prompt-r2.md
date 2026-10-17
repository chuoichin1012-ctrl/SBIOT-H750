# Code review r2: branch `ntp-defaults`

Your r1 P2 (a restart of SNTP leaves a pending DNS callback alive, so two requests go at once) is addressed in de97938 (`git show de97938`):

- SNTP is never restarted.
  - `sysmgr_start` only records the server and sets `s_ntp_on`, under the lwIP core lock.
  - If a route is already up (`netmgr_route_name()` is not "NONE", read under the same lock), it calls `sysmgr_ntp_kick()` itself.
- `sysmgr_ntp_kick()` runs in the lwIP core (`netmgr route_check` after `dns_apply`, or `sysmgr_start` under the lock).
  - It starts SNTP once: `s_ntp_started` guards `sntp_setoperatingmode`, `sntp_setservername` and `sntp_init`.
  - Later route changes leave it to SNTP's own retries (15–60 s) and hourly updates.
- `netif_default` is not used as the route test: netmgr sets it at init (`netmgr.c:289`), before any route.
- **On board 2, with lwIP SNTP/DNS debug:**
  - `sntp_init` at 5.15 s, the moment the route came up;
  - one DNS request, one NTP request;
  - the clock set at 5.34 s.

Findings as P1 / P2 / minor with file:line. End with **APPROVED** or **CHANGES NEEDED**.
