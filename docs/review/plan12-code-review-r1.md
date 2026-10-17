## Critical

None found.

## Important

1. **Multicast can bypass the reserved-port check and leave a UART bridge unusable.**  
   [rules.ts:32](/home/linh/projects/SBIOT-Gateway/stm32/web/src/lib/rules.ts:32)

   Configure UDP client, multicast, `localPort=0`, `remotePort=48600`. The UI accepts this, but [bridge.c:746](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:746) uses `remotePort` as the local bind port in this mode. Discovery already owns that port, so the bridge enters `ERROR` after restart.

   **Fix:** check the effective local port, including this multicast fallback. Add tests rejecting that combination while allowing multicast with a different explicit local port.

## Minor

2. **Received JSON documents are never disposed.**  
   [Packets.cs:62](/home/linh/projects/SBIOT-Gateway/stm32/tools/finder/Sbiot.Finder.Protocol/Packets.cs:62)

   Every received JSON datagram allocates a `JsonDocument`, including unrelated replies, without returning its pooled buffers through `Dispose()`.

   **Fix:** parse within a `using` scope and either construct the result there or return a cloned root for matching packets.

3. **The “longest found” test misses JSON escaping expansion.**  
   [test_disc_core.c:92](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/host/test_disc_core.c:92)

   A 16-byte hostname containing control characters expands substantially when escaped. With the test’s maximum numeric/address fields, the reply reaches **518 bytes** and is silently omitted. Ordinary UTF-8 names fit.

   **Fix:** cover escaped characters and bound the discovery hostname’s encoded representation so discovery remains available.

The store locking and cJSON ownership paths look correct. The vendored lwIP path confirms that `SO_BINDTODEVICE` selects Ethernet before default-route lookup.

The existing `test_disc_core` binary passed with leak detection disabled because LeakSanitizer cannot run under this sandbox. Windows GUI/multi-adapter testing and LTE-default hardware verification remain pending as disclosed.

**Verdict: CHANGES NEEDED.**