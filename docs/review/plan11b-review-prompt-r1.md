Please review the plan stm32/docs/plans/2026-10-17-11b-bench-hil.md (read-only) against spec §12.2/§12.3/§6.1/§11.4, the roadmap row 11 and the bench scripts in stm32/firmware/test/bench.

Context (the user, 2026-10-17): the board is now on a routed LAN with DHCP (192.168.1.9); the Windows bench host reaches it by Wi-Fi 192.168.1.3 (its Ethernet 192.168.0.10 no longer to the board; the Wi-Fi profile is Public, and a firewall rule already allows C:\Python311\python.exe inbound on Private and Public); DO1 is wired to DI1, DO2 to nothing; the RS485 loop UART1-UART2; R103 not fitted; the SIM is still locked (no LTE, no factory reset, priority stays 0). Execution: Native, then your code review before the PR.

Please check: whether bench_env.py is a sound single source (the env, then a FinderTool search); what else in the scripts assumes the old bench and is missing from the plan; the order and isolation of the suites; whether the gates are measured honestly (and the "needs equipment" list is complete and not an excuse to skip something this bench can do); the handling of net_check/disc_check so the board stays reachable; anything risky to the board or the SIM.

Reply with findings ranked (Critical / Important / Minor), then a verdict: APPROVED or CHANGES NEEDED.
