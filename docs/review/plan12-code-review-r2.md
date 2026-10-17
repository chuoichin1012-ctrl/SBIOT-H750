**Critical:** None found.

**Important:** None remaining. The multicast check now matches the effective bind port used by `bridge.c`.

**Minor — previously accepted deferrals:**

- `stm32/tools/finder/Sbiot.Finder.Protocol/Packets.cs:62`: received `JsonDocument` instances remain undisposed.
- `stm32/firmware/components/services/disc_core.c:165`: control characters in a saved hostname can expand the `found` reply beyond 512 bytes, preventing discovery.

No new blocking findings in the branding, embedded resources, or FinderTool rename.

**Verdict: APPROVED.**