## Critical

None found.

## Important

1. **The flood test can pass without exercising the fix.**  
   [Task 3, lines 1084–1095](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:1084)

   The Windows process’s exit status and packet count are not checked. A failed sender followed by a successful search passes. Also, 20 seconds of starvation can end before syslog’s 60-second watchdog deadline, so “same boot afterward” does not prove lower-priority tasks ran.

   **Fix:** require successful execution and a meaningful packet count; verify lower-priority progress during the flood, or extend the sustained-load check beyond the watchdog deadline. The proposed `vTaskDelay(1)` itself addresses the code defect.

2. **IPv4 validation can admit input that throws from the Save handler.**  
   [Task 5, lines 1370–1384](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:1370)

   .NET `\d` includes Unicode decimal digits. For example, `192.168.0.٧` passes `IsQuad`, but `IPAddress.Parse` rejects it. In static mode, this throws `FormatException` from `Validate()` through the unguarded dialog Save handler. This conclusion follows from the [documented regex semantics](https://learn.microsoft.com/en-us/dotnet/standard/base-types/character-classes-in-regular-expressions#decimal-digit-character-d) and [.NET’s IPv4 parser](https://github.com/dotnet/runtime/blob/v10.0.0/src/libraries/Common/src/System/Net/IPv4AddressHelper.Common.cs).

   **Fix:** replace every `\d` with `[0-9]`, use whole-string anchors `\A…\z`, and test rejection of non-ASCII digits without throwing.

## Minor

3. **The OOM success assertion still does not compare the whole document.**  
   [Task 1, lines 136–138](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:136)

   It checks four substrings; losing `netmask`, DNS or another member could pass. Compare the parsed result against a complete expected document, prepared outside fault injection.

4. **The publish block does not stop on failure.**  
   [Task 6, lines 1892–1898](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-17-12-lan-discovery.md:1892)

   `pipefail` exposes pipeline failures but does not stop subsequent commands. A successful final test can still leave the overall block successful after publishing or copying failed.

   **Fix:** use `set -e -o pipefail` for this GREEN build/publish block, or connect dependent commands with `&&`. Keep expected RED commands separate.

The other listed r1 fixes are present, including synchronous binding, strict datagram validation, socket-error handling and native-library bundling. No Windows or board checks were run during this read-only review.

**Verdict: CHANGES NEEDED.**