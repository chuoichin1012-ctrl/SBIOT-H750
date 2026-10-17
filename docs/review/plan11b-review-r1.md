**Critical:** None found.

**Important**

1. **The scheduled suites can still activate LTE.**  
   **Plan Task 2 Step 2; Task 3 suite `store`.** `net_check.py:30–34` writes partial `network` documents; omitted `priority` becomes the default **1**. More seriously, `store_check.py:96–111` deliberately fails SPI4 initialization: `app_main.c:84–98` then loads default network settings and starts LTE. Checking priority before the suite does not protect either path.  
   **Fix:** construct every network test configuration from the complete saved document, explicitly preserving priority 0. Defer the app-side SPI4-init-failure case while the SIM is locked, unless modem inactivity is independently enforced.

2. **Discovery failure silently selects an unverified board address, and DHCP changes leave stale clients.**  
   **Plan Task 1, lines 126–135; Task 2 Step 1.** `resolve_gw()` falls back to `192.168.1.9` when discovery fails. That defeats the promised current-address resolution and could target another device after lease reassignment. Also, updating discovery results does not update the copied `web_check.HOST` / `bridge_check.GW` values after DHCP restoration.  
   **Fix:** fail clearly when discovery fails; validate the reply’s protocol, request ID, SN and usable address. After address-changing restarts, explicitly update the HTTP/socket targets before subsequent checks. Add tests for discovery failure and a changed lease.

3. **Running suites sequentially does not establish their required initial state.**  
   **Task 3 Step 2; `tls_check.py:7–9`, `case_certs`, `case_mutual`.** TLS assumes no existing client key and an unset clock. Previous runs leave certificates behind; `case_certs` executes before `setup()` disables NTP and restarts. Its documented preparation—`Z WIPE`—would erase priority 0 and reactivate LTE.  
   **Fix:** specify a safe baseline and cleanup for each suite, including running versus saved configuration. Explicitly prohibit WIPE. If the missing-key case cannot be prepared safely, record that case as blocked; still run the remaining TLS checks. Preserve existing calibration and identity.

4. **End-of-run health counters cannot qualify the preceding workloads.**  
   **Task 3 Step 3, line 204.** Watchdog maxima, serial counters and pool statistics reset across the many reboots. Reading them after OTA measures the final boot, not the earlier stress. Furthermore, “no supervisor restart since the run began” contradicts the deliberately injected `supervisor_check hang`.  
   **Fix:** capture gates during each workload and before its reset, with boot identity and expected-reset annotations. Exclude only the explicitly injected supervisor failure. Collect logs continuously enough that ring rollover cannot erase failures.

5. **The two required memory scenarios are not covered, although this bench can generate them.**  
   **Task 3 suite `memory`; Task 4 line 219; spec §12.3.** `mem_check.py` does not create scenario A’s 32 TCP clients plus four WebSockets. Its TLS workload also does not establish simultaneous handshakes, a full outbox, slow UI reader and GC together. It prints several memory figures without asserting all §12.3 thresholds. These are software-generated loads, not missing equipment.  
   **Fix:** add explicit A/B cases and assertions for stack headroom, minimum heap, largest block and pool limits. Verify that the intended connections/load actually exist. Otherwise label them **not implemented/not run**, not “needs equipment.”

6. **Two additional obsolete expectations need explicit migration.**  
   **Task 3; `boot_check.py:89–90`, `edge_check.py:353`.**
   - Boot still requires watchdog kicks every **200 ms**, although the task now runs every **50 ms**.
   - Edge still expects `sys_time` / `sys_unix_time` placeholders without real time, contradicting the merged device-clock behaviour.  
   **Fix:** update these assertions to the current contracts before running the suites. Also reconcile spec §12.2/§6.1’s old **WDI <400 ms** flood criterion with §11.4’s **WDI <600 ms**; normal WDI spacing is already 500 ms.

**Minor**

- **Task 2 Step 3:** add `pipefail` and retain full logs here too; currently `tail` can hide a failing exit status.
- **Task 1 `check_peer()`:** finding an allow rule is a preflight hint, not proof of connectivity. Missing interface/profile or PowerShell failure must produce a diagnostic, not a firewall verdict; restricted rule scopes or explicit block rules can still prevent peers.
- **Task 4 equipment list:** add two independently controlled current channels for ADC crosstalk and a step-capable source for latency. Also schedule the existing `boot_check --hostile` cases: they need the available SWD setup, not additional equipment.

**Verdict: CHANGES NEEDED.**