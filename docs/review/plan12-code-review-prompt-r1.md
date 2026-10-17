Please do the code review of Plan 12 (read-only): branch `discovery`, commits 38a273d..423756b (`git diff 38a273d..423756b -- stm32`). Plan: stm32/docs/plans/2026-10-17-12-lan-discovery.md (you approved r3). Spec: stm32/docs/specs/2026-10-17-lan-discovery-design.md.

What was done and verified:
- Task 1 store_patch (store.c): test_store and test_store_small ok; the lock trace caught a mutation (UNLOCK/LOCK between read and write).
- Task 2 disc_core.c + disc_vectors.json + test_disc_core: ok; disabling strict() failed the 5 strictness vectors.
- Task 3 disc.c, netmgr_eth_ifname, app_main (disc_start before services_start), win_peer.py disc mode, disc_check.py. App builds, check_elf ok. On the board (web OTA): disc_check 19/19 ok — search, bad set, restart without mac ignored, set to 10.10.0.7/24 and found from Windows 192.168.0.10, set back, network config identical afterwards, a 70 s flood of 3.7 M datagrams without a restart, UART1 as a UDP server on 48600 → still found and UART1 in ERROR, restart. /api/status hbMissing 0, iwdgMaxGapMs 50, wdiMaxGapMs 500.
- Task 4 rules.ts discPortClash + Port.tsx: npm test:unit 17/17, tsc ok.
- Task 5 Sbiot.Finder.Protocol: dotnet test 32/32 on Linux; the regex mutation (\d) was caught.
- Task 6 WinForms Sbiot.Finder: builds with 0 warnings on WSL; single-file publish, 52 MB (compressed); the user's Windows trial is pending.

Rulings taken during execution:
Task 2: note: test passed at first GREEN; mutation (strict() disabled) failed the 5 strictness vectors; restored. Ruling: RED was 'no rule for disc_core.c' rather than the plan's 'disc_core.h: No such file' — same cause (module missing) — cost if wrong: none.
Task 3: Ruling: DTCM budget (120 KB) exceeded with the 6 KB disc stack in .bss — moved s_stack and the 513-byte receive buffer to .axi_bss (as the plan allowed); AXI 97.78%, heap is a fixed 352 KB array so unchanged — cost if wrong: an AXI stack is slower than DTCM (disc is not time-critical).
Task 3: note: board bench disc_check 19/19 ok (log in workspace); /api/status system.hbMissing 0, iwdgMaxGapMs 50, wdiMaxGapMs 500. Ruling: web_check.py's 'ota' case needs the build arg via the CLI form, so the OTA ran as w.case_ota('../../build/h750') — cost if wrong: none.
Task 4: Ruling: `errors` typed Record<string, string> (tsc TS7053 on the spread form) — cost if wrong: none.
Task 4: Ruling: `errors` typed Record<string, string> (tsc TS7053 on the spread form); www/ is generated and git-ignored, so not committed (the plan listed it) — cost if wrong: none.
Task 6: Ruling: added EnableCompressionInSingleFile (116 MB -> 52 MB exe) — the user downloads one file; startup decompresses in memory (slightly slower start) — cost if wrong: a property to remove. Firmware rebuilt with the new www (task 4) and installed by web OTA before the Windows trial.

Review Focus from the plan: concurrent Web/Finder saves of network; multi-adapter PCs; 512/513 bytes and the longest found; two gateways answering; LTE as the default route (SO_BINDTODEVICE before bind — not runnable while the SIM is locked, please check the code path).

Reply with findings ranked (Critical / Important / Minor) with file:line, then a verdict: APPROVED or CHANGES NEEDED.
