- **P2 — [sysmgr.c:115](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/sysmgr/sysmgr.c:115): restarting SNTP leaves an outstanding DNS callback alive.** If the route changes while the NTP hostname is resolving, `sntp_stop()` removes the PCB and timers but does not cancel that lookup. `sntp_init()` registers another callback. When DNS completes, lwIP invokes both callbacks and sends two NTP requests back-to-back; repeated route changes can add more. This bypasses the intended request spacing and risks the server throttling already observed.

  Keep only one outstanding SNTP lookup/request, coalescing route-change kicks while it is pending. Add a regression test: start an unresolved lookup, change route, complete DNS, and assert only one NTP request is sent.

No further findings in the factory defaults or timezone changes. Existing, up-to-date `test_cfgmodel` and `test_edge_core` binaries passed with leak detection disabled; no board tests run.

**CHANGES NEEDED**