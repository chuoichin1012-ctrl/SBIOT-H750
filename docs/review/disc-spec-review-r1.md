## Critical

None identified.

## Important

1. **Success is acknowledged before the operation succeeds.**  
   [Discovery spec:72–76](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-17-lan-discovery-design.md:72)

   `set` sends `ok:true` before `store_set_text()`, yet that function can fail because of allocation, cache capacity or flash errors. `factory` similarly acknowledges success before a reset that can return `STORE_EFLASH`.

   **Fix:** authenticate and consume the nonce → perform the operation → acknowledge its actual result → schedule restart. For factory reset, document that an incomplete reset still schedules a restart, as [store.c:946](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:946) does today.

   Also reconcile the tool’s timeouts: flash work can exceed the three-second ACK wait, and restart includes up to **60 seconds of LTE shutdown**, before boot starts ([sysmgr.c:118](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/sysmgr/sysmgr.c:118)). A single search after 15 seconds may show the old address. Poll for completion with a bounded timeout; an ACK timeout means “outcome unknown,” not permission to repeat a destructive command automatically.

2. **The tool cannot authenticate an ACK.**  
   [Discovery spec:62,76](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-17-lan-discovery-design.md:62)

   A LAN participant can observe `id` and MAC, then forge `ok:true` or `err:auth`. The technician can be told that settings were saved or erased when nothing happened. The request’s `id` is also outside its HMAC.

   **Fix:** include `id` in the request HMAC; authenticate result ACKs using the same key and a distinct response prefix, covering request nonce, ID, MAC, operation and result. Discovery can remain unauthenticated, but the tool must treat `found` data as untrusted—not proof of device identity.

3. **“Preserve the other network fields” needs an atomic, saved-config merge.**  
   [Discovery spec:73](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-17-lan-discovery-design.md:73)

   `store_set_text()` replaces the whole config. A separate read–merge–write can overwrite an APN/priority change saved concurrently through the web. Using `store_get()` is also unsuitable for exact preservation: it runs `cfg_load()` and reconstructs schema fields.

   **Fix:** read the latest saved JSON, replace only the seven Ethernet members, and save under one store transaction/recursive lock. Preserve the other JSON members, including unknown ones; fail without writing on parse/allocation failure. Add a concurrent web/Finder update test.

4. **`found` mixes saved settings, running settings and potentially LTE DNS.**  
   [Discovery spec:47–48,89](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-17-lan-discovery-design.md:47)

   After a web save without restart, saved `ipMode` can describe static mode while the reported address is still a DHCP lease. More concretely, [netmgr_status():363](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/netmgr.c:363) returns **global selected-route DNS**. With priority 2, those can be LTE DNS servers, not Ethernet DNS. Prefilling Network… from this reply can save the wrong values.

   **Fix:** distinguish the saved seven-field Ethernet configuration from the current Ethernet address/status. Report the actual running web port for “Open Web UI.” Do not use global DNS as Ethernet configuration.

5. **Nonce failure and consumption rules are incomplete.**  
   [Discovery spec:49,70–72](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-17-lan-discovery-design.md:49)

   `board_rng()` can fail after writing only part of its output. The spec does not define that path. It also says both “after every authenticated command” and “after validation succeeds,” leaving authenticated malformed requests ambiguous.

   **Fix:** define:
   - a fresh nonce at boot, with monotonic expiry;
   - no authenticated mutations while nonce generation has failed;
   - invalidation of the consumed nonce before side effects, including when saving later fails;
   - no zero/predictable fallback or reuse after failed regeneration;
   - an explicit consumption rule for authenticated-but-invalid requests.

   Test RNG failure, reboot replay, duplicate commands and two tools using the same nonce.

## Minor

- **Reserve UDP port 48600 explicitly.** [Spec:24](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-17-lan-discovery-design.md:24): bridge UDP currently binds a configurable local port ([bridge.c:744](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:744)). Define startup precedence and the visible error for an existing conflicting configuration; prevent new conflicts in the UI.

- **Tighten serialization wording, not the architecture.** [Spec:60–71](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-17-lan-discovery-design.md:60): validate field types and bounded lengths before constructing the HMAC input; specify duplicate-key rejection, embedded-NUL rejection and exact nonce encoding. With validated IPv4 strings and fixed-format nonce/MAC, the delimiter format is sufficient—general JSON canonicalization is unnecessary. Replace “every operation has data-independent timing” with constant-time authentication-tag comparison.

- **Document the actual password/lockout limits.** [Spec:139](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-17-lan-discovery-design.md:139): a captured command permits offline password guessing; the five-attempt lockout cannot prevent that. Unauthenticated traffic can also deliberately trigger the global lockout. These are limitations of the chosen design, not a reason to replace the user’s HMAC decision. HMAC’s security depends on its secret key. [RFC 2104](https://www.rfc-editor.org/rfc/rfc2104.html#section-3)

The **lwIP approach is feasible and can stay simple**: bind an IPv4 UDP socket to `0.0.0.0:48600`, enable broadcast, and use `SO_BINDTODEVICE` with Ethernet’s actual interface name. The vendored [sockets.c:3513](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/lwip/src/api/sockets.c:3513) implements this through `udp_bind_netif`; UDP checks the incoming interface and selects the bound outgoing interface before normal routing. No raw API or lwIP patch is needed.

Explicitly broadcast **commands as well as searches** for the cross-subnet case. Add a Windows bench gate with different PC/gateway subnets, multiple adapters and LTE as default route, confirming that no discovery traffic leaves PPP. Limited broadcast is confined to the local network segment. [Microsoft UDP documentation](https://learn.microsoft.com/en-us/dotnet/framework/network-programming/using-udp-services)

**Verdict: CHANGES NEEDED.**