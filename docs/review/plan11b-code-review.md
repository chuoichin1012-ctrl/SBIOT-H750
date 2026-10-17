No P1 findings.

**P2**

1. **Scenario A passes with Edge stopped — `stm32/firmware/test/bench/mem_check.py:279`.**  
   `load` checks TCP clients, WebSockets and MQTT, but only prints Edge’s state. I reproduced all scenario-A checks passing with `edge.state = "NO MEMORY"`. Require the 200-point runtime and a received report containing the expected 100 values before qualifying memory. The report’s scenario-A PASS at `stm32/docs/reports/2026-10-17-11b-bench.md:54` needs that evidence.

2. **Scenario B does not establish the slow-reader load — `stm32/firmware/test/bench/win_peer.py:453`, `mem_check.py:352`.**  
   `slowget` requests `/`, whose current compressed HTML is only 284 bytes: it can finish transferring immediately without sustained backpressure. Its results are then discarded, so even a connection failure cannot fail the scenario. Request a large UI asset, verify successful data reception, and establish that the transfer remains active during the measurement.

3. **GC need not overlap the TLS handshakes — `stm32/firmware/test/bench/mem_check.py:355`.**  
   Three handshakes overlapping near startup, followed by GC much later, satisfies both assertions. That does not demonstrate the combined scenario in §12.3. Correlate GC evidence with the handshake-overlap window, or report B as partial. The current unconditional PASS in the report at line 55 is too strong.

4. **Memory cases do not restore their changes — `stm32/firmware/test/bench/mem_check.py:294` and `:364`.**  
   A leaves both ports, system settings and the Edge model changed; B leaves TLS port settings and replaces the hostname with `"saved-name"`. MQTT settings are replaced rather than restored. Exceptions also bypass cleanup of several peer processes/sockets. Snapshot modified configurations and restore them in `finally`, with peer cleanup there too.

5. **Network cleanup fails when the address move fails — `stm32/firmware/test/bench/net_check.py:170`.**  
   If the board applies the new static address but `follow()` fails, `w.HOST` remains stale. The first HTTP call in `finally` raises, preventing restoration of `home`. Add the console/discovery fallback used by `disc_check`, restoring the whole saved document.

6. **Gate rows can conceal a reset or missing evidence — `stm32/firmware/test/bench/bench_env.py:87–96`.**  
   Discovery can return boot N, then the board resets before `/api/status`; the row combines boot N with fresh counters from N+1 and passes `judge`. Missing `mem.pools` also becomes an empty “no full pools” result. I reproduced acceptance of both conditions with mocks. Verify boot identity around the status read and require the expected counter structures.

**Minor**

- **TLS baseline handling promised by the plan remains absent — `stm32/firmware/test/bench/tls_check.py:173`.** A repeat run still asserts that no client key exists, although an earlier run installed one. `case_certs` also runs before the setup that clears real time. Implement the planned baseline/blocked-case handling.
- **RAM log streaming still skips retained lines during rollover — `stm32/firmware/components/webapi/web.c:129`.** This is pre-existing: after sending 20 lines, if ten oldest lines roll out, skipping 20 in the next snapshot loses ten additional, still-retained lines. The new flash cursor fixes the flash phase, not this limitation.

The flash-phase termination and initial `from = 0` handling look correct. The new cursor narrows a 64-bit FlashDB sequence to 32 bits, but exhaustion is not a practical near-term issue at the configured logging rate.

Local `test_bench_env` and all five `test_mem_check` tests passed. No hardware tests were run.

**CHANGES NEEDED**