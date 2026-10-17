Thank you for r1 (stm32/docs/review/plan11b-review-r1.md). Revised stm32/docs/plans/2026-10-17-11b-bench-hil.md; please re-review (read-only):
1. LTE: Global Constraints forbid store_check case 5 (app SPI4 init fails → defaults → priority 1) and Z/WIPE; every network a suite writes is the whole saved document with priority 0 kept (net_check rebuilt so); ota_check's SPI4 case is the bootloader's (the app's store mounts) and stays.
2. bench_env: no fallback address — resolve_gw raises RuntimeError naming the SN; pick() validates sbiot/v/op/id/sn and a usable IPv4; set_board(ip) updates web_check.HOST and bridge_check.GW; net_check and disc_check call it after every move; tests for a failed search, bad replies and a changed lease.
3. Baselines per suite; never WIPE; tls_check's setup (NTP off, restart) before case_certs; a no-client-key case recorded blocked when a key exists; calibration and binding untouched.
4. Gates per workload: bench_env.gates(label) inside the stress cases before any restart (serial continuous+flood, bridge/mqtt/disc floods, store's 1000 saves, an OTA over UART7 with reset=False), the boot number on each row; /api/log saved after each suite; supervisor_check's injected stops excluded.
5. New Task 4: scenarios A and B of §12.3 in mem_check, each load verified to exist, the criteria asserted (stacks ≥ 1 KB, heap.min ≥ 16 KB, largest block — "not measured" if the status lacks heapLargest — pools), a load that cannot be produced reported "not run".
6. Migrations: boot_check/store_check 50 ms, edge_check sys_time on the device clock, io_check flip, spec §12.2 flood criterion iwdg < 400 / wdi < 600.
Minors: pipefail and logs in Task 2; check_peer returns (ok, reason) and is a hint only; the equipment list adds two independent channels and a step source; boot_check --hostile scheduled (SWD).

Reply with remaining findings ranked (Critical / Important / Minor), then a verdict: APPROVED or CHANGES NEEDED.
