Please review the implementation plan stm32/docs/plans/2026-10-17-12-lan-discovery.md (read-only) against its spec stm32/docs/specs/2026-10-17-lan-discovery-design.md (approved by you in r4 with the duplicate-key fix) and the code under stm32/firmware, stm32/web.

The user's decisions are fixed: no authentication; every packet broadcast; WinForms; keep it simple. Execution method: Native (I implement task by task with TDD, then your code review before the PR).

Please check:
1. Spec coverage: every requirement has a task; nothing extra.
2. Correctness of the code in the plan against the real APIs: store.c internals (cache, LOCK nesting, store_set_text), cJSON calls and ownership, lwIP sockets (SO_BINDTODEVICE with netif_index_to_name, SO_RCVTIMEO, broadcast send/receive with IP_SOF_BROADCAST 0), netmgr_status fields, sysmgr_restart_later, store_factory_reset, the supervisor API, the app start order (disc before services_start).
3. The tests: do they fail first for the right reason and pin the behaviour (lock-trace, OOM sweep, vectors shared byte for byte between C and C#, 512/513, nesting, longest found)?
4. The bench (disc_check.py via win_peer.py on Windows, WSL behind NAT): does it leave the board as it found it and never send factory?
5. The .NET part: builds on Linux (EnableWindowsTargeting), Utf8JsonWriter output matching the vectors, UdpClient bound to the adapter address receiving broadcast replies on Windows, WinForms code sanity.

Reply with findings ranked (Critical / Important / Minor) with file/step references, then a verdict: APPROVED or CHANGES NEEDED.
