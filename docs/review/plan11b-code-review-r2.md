No P1 findings. The six reported fixes are addressed, but one P2 remains.

- **P2 — `stm32/firmware/test/bench/mem_check.py:409`: failed measurements can qualify as passing.**  
  `status()` returns `None` for a non-200 response, and `if st` silently removes that sample. If `/api/status` returns 500 under TLS/GC memory pressure for every in-window read, `bad` is empty and the memory criteria pass. The final gates run after the peers have stopped, so recovery can conceal the failure. Reproduced locally with all samples missing. Require successful status reads with the expected fields; add a test proving missing measurements cannot pass.

- **Minor — `stm32/firmware/test/bench/test_net_cleanup.py:41`: the mocked test still launches the real Windows flood.**  
  `nc.main()` reaches `subprocess.Popen`, which the test does not replace. The local run attempted Windows execution. Inject the intended failure earlier, or mock subprocess execution, so this host test cannot generate LAN traffic.

Scenario A now checks Edge and its report. Scenario B’s **partial** classification accurately reflects the missing GC/handshake correlation. The previously deferred minors remain unchanged.

Local helper tests passed; no board validation performed.

**CHANGES NEEDED**