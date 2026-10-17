# Code review: branch `unix-utc`

Please review `git diff main..unix-utc -- stm32` (one commit).

- **`sys_unix_time` is UTC again.** The user agreed after being asked: Unix time is defined in UTC, and a server adds its own zone, so a shifted value would be off twice.
  - `sys_time` stays local text in the gateway's zone (PR #36).
  - Change: `edge_sys_value`; tested in `test_edge_core` (UTC+7 and power-on: the same UTC seconds whatever the zone).
  - The spec, the UI help and the `edge_check` expectations are updated: the power-on range starts 2019-12-31 00:00 UTC, and the local `sys_time` is compared with the UTC `sys_unix_time` plus the zone.
- **A regression from PR #36:** `web/src/pages/edge/Report.tsx`'s help string was single-quoted around "gateway's", which is a syntax error.
  - It is double-quoted now.
  - `tsc --noEmit`, the unit tests (34) and `build:firmware` pass.
  - The embedded UI on the board had been built before PR #36, so nothing shipped broken.
- **On board 2:** the new build boots, the clock is set 0.2 s after the route, and the web answers.

Findings as P1 / P2 / minor with file:line. End with **APPROVED** or **CHANGES NEEDED**.
