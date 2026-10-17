# Code review r2: Plan 11b, branch `bench-hil`

Your r1 (`stm32/docs/review/plan11b-code-review.md`) asked for changes. They are in commit 2a56b4b; please review it (`git show 2a56b4b`) and the branch as a whole.

- **P2-1:** scenario A now requires the Edge `RUNNING n/200` with n ≥ 42 and a report at the broker with exactly the template's 100 keys.
  - The 5 Modbus TCP devices share one `tcpserver modbus=1` peer on `ec.MBTCP`.
  - The bus points have no slave: the ports are TCP servers, and the loop ties UART1 to UART2.
  - On the board: RUNNING 42/200, 100/100.
- **P2-2:** `slowget` reads the gzip index, takes its script (`/assets/app.js`, 43 807 B gzip) and reads it at 256 B/s with a 1 KB receive buffer. It emits `slow` events every 5 s. The case requires the count to grow, and no `done` or `error` before the window ends.
- **P2-3:** scenario B is reported **partial** (GC and the overlap in the same window, not inside it), in the case's output and in the report.
- **P2-4:** `snapshot()` / `restore()` of mqtt, mqttPublish, mqttSubscribe, port1, port2, system, edge, edgeDevices, edgeReport and edgeLinkage, then a restart. Peers and sockets are stopped in `finally`.
- **P2-5:** `net_check`'s `finally` follows the board, reads the saved document tolerating `OSError`, and falls back to the console `N` with the whole document plus NRST. Covered by `test_net_cleanup.py` (fails without the fix).
- **P2-6:** `gates()` searches again after `/api/status` and raises if the boot changed; a missing `mem.pools` list is an error. Covered by `test_gates_one_boot`.
- **Minors, deferred** (ledgered): the `tls_check` baseline; the RAM-log phase skipping by count (pre-existing).

Board reruns: `mem_check a` ok, `mem_check b` ok (verdict partial, every criterion met).

Findings as P1 / P2 / minor with file:line. End with **APPROVED** or **CHANGES NEEDED**.
