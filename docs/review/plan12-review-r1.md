## Critical

None identified.

## Important

1. **Task 3: creating `disc` first does not reserve its port first.**  
   [Plan:850](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:850)

   `disc_start()` creates a priority-5 task and returns. `init` runs at priority 7, while bridge tasks run at priority 8. A bridge configured for UDP 48600 can therefore bind before discovery, leaving discovery retrying forever—the opposite of the specified precedence.

   **Fix:** finish binding the discovery socket before `services_start`, either synchronously or through a bounded startup handshake. Test an existing conflicting bridge configuration.

2. **Task 2: the parser accepts packets outside the specified format.**  
   [Plan:526](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:526)

   `cJSON_ParseWithLength()` does not require consuming the entire datagram. A valid `factory` object followed by garbage still executes. cJSON also accepts some invalid JSON number forms. Furthermore, `strlen`/`strcmp` accept a valid prefix before an escaped NUL, so `"mac":"02:95:b8:e9:a7:9f\u0000other"` passes the MAC check.

   **Fix:** validate the complete datagram using the existing strict scanner before cJSON parsing, and reject embedded NUL in protocol keys/fields. Add shared vectors for trailing garbage, invalid number syntax and escaped-NUL fields.

3. **Task 3: continuous receive traffic can starve lower-priority tasks.**  
   [Plan:829](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:829)

   A continuously nonempty socket never reaches its receive timeout. Ignored packets and empty datagrams also cause no delay. At priority 5, discovery can starve the priority-3 Reload worker and priority-2 syslog; syslog’s 60-second supervisor deadline can then reset the gateway.

   **Fix:** apply the existing bridge pattern: process a bounded batch, then `vTaskDelay(1)`, counting rejected/empty datagrams too. Add a receive-flood bench check.

4. **Task 5: validation rejects a legitimate isolated LAN configuration.**  
   [Plan:1285](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:1285)

   `192.168.0.7/24` with gateway `0.0.0.0` is rejected, although the spec requires checking the gateway’s subnet **if a gateway is supplied**, and firmware accepts zero. Requiring nonzero DNS1 also adds an unapproved restriction for installations without DNS.

   **Fix:** support `0.0.0.0` as “none”; apply gateway relationship checks only when nonzero. Add tests for an isolated static LAN.

5. **Task 6: ordinary socket failures escape the WinForms event handlers.**  
   [Plan:1643](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:1643), [1671](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:1671)

   Removing an adapter after enumeration, or a send/receive socket failure, throws through the `async void` click handlers. `Search` also lacks `finally`, leaving its button disabled if execution continues after the exception.

   **Fix:** catch network errors at the UI operation boundary and restore controls in `finally`. If a command may already have been sent, report “outcome unknown” without resending. Microsoft explicitly recommends handling exceptions inside asynchronous WinForms event handlers. [Official guidance](https://learn.microsoft.com/en-us/dotnet/desktop/winforms/forms/events)

6. **Task 6: copying only the published EXE is not sufficient with these publish options.**  
   [Plan:1771](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:1771)

   `PublishSingleFile=true` bundles managed assemblies but leaves native runtime libraries alongside the executable by default. The next command copies only `SbiotFinder.exe`, so the promised standalone distribution is incomplete.

   **Fix:** add `IncludeNativeLibrariesForSelfExtract=true`, then test the EXE alone in a clean directory on Windows. [Microsoft single-file deployment documentation](https://learn.microsoft.com/en-us/dotnet/core/deploying/single-file/)

## Minor

7. **Task 1’s lock test is weaker than the approved lock-trace test.**  
   [Plan:68](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:68)

   Counting one final release does not prove the read happened under the lock. Moving the read before `LOCK()` would still pass.

   Record the read/write events with lock depth and release generation, as specified. Also make the OOM sweep verify the complete expected document on `STORE_OK`; currently any success is accepted without checking its contents.

8. **The bench does not prove preservation of the original LTE fields.**  
   [Plan:1005](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:1005)

   Checking `priority == 0` and APN merely being nonempty would pass after an APN was changed incorrectly. Capture the original configuration before mutation, require the bench’s expected starting state, and compare all untouched fields afterward. The script contains no factory invocation and does attempt restoration in `finally`, which is appropriate.

9. **Build/test pipelines can conceal failure and inspect stale artifacts.**  
   [Plan:873](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:873), [1770](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:1770)

   Without `pipefail`, `build | tail` reports `tail`’s success. `check_elf` or the EXE-copy step can then use an older artifact. Preserve the producer’s exit status and stop before subsequent gates or copying.

The underlying `store_patch` ownership handling, recursive lock usage, netmgr fields, supervisor calls and restart/factory APIs are compatible with the current code. The lwIP socket options are valid: `IP_SOF_BROADCAST=0` disables broadcast filtering, not broadcast support. `EnableWindowsTargeting` is correctly present; actual Windows broadcast reception remains a bench gate.

**Verdict: CHANGES NEEDED.**