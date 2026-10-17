**Critical:** None found.

**Important**

1. **The gate recorder reads a nonexistent boot field.**  
   **Task 1, `gates()`; Task 3 Step 3.** `/api/status.system` has no `boot` member (`components/webapi/api.c:140–154`), so every recorded boot number will be `null`. An unexpected reset during a workload could therefore replace the counters being qualified.  
   **Fix:** obtain the boot count from FinderTool’s existing `found.boot` or the console, compare it before/after each workload, and reject missing status/counters. Read largest-block data from **`system.heapLargest`**, which already exists; it is not under `mem`.

2. **Scenario B still lacks a workable sequence proving simultaneous load.**  
   **Task 4 Step 2.** A stopped broker cannot perform the required MQTT TLS handshake. Conversely, reconnecting normally can drain the full outbox before measurement. Restarting three peers together also does not establish overlapping handshakes, and 100 saves alone does not prove GC occurred.  
   **Fix:** specify the sequence and evidence: fill the outbox offline, then allow the MQTT TLS handshake while withholding MQTT CONNACK so queued messages remain; coordinate the other TLS peers and slow reader, and verify GC during that interval. Record handshake overlap and outbox/GC evidence. If overlap is not achieved, report partial/not run rather than a scenario-B pass.

**Minor**

- **Task 1, `pick()`:** the proposed IPv4 check still accepts broadcast/multicast addresses and leading zeros; `isdigit()` also accepts characters that `int()` cannot parse. Use `ipaddress.IPv4Address`, reject unsuitable destination addresses, and add malformed-address tests.
- **Task 3 Step 1b:** when a client key already exists, skip only the **missing-key assertion**, not the successful mutual-TLS portion of `case_mutual`. The latter remains runnable after uploading the matching certificate/key.
- **Task 3 Step 1:** update the obsolete WDI `<400 ms` wording in **§6.1 as well as §12.2**; both currently contain it.

The revised SIM exclusions and network-document preservation address the earlier safety findings.

**Verdict: CHANGES NEEDED.**