Please review a new design spec for the STM32 gateway (read-only):

- stm32/docs/specs/2026-10-17-lan-discovery-design.md (new)
- stm32/docs/specs/2026-10-14-stm32-firmware-design.md §10.5 (a pointer only)

The feature: like the USR-M100 Windows configuration utility, a SBIOT Windows tool (WinForms, .NET 10) finds gateways on the LAN by UDP broadcast (port 48600), shows MAC/SN/IP, opens the Web UI, and can change the Ethernet settings, restart and factory-reset a gateway. Changing commands carry an HMAC-SHA256 keyed with the web admin password over a nonce from the gateway's `found` reply.

User decisions (fixed): own protocol (not USR-compatible); Windows GUI in WinForms; web password + HMAC; keep it simple; configs are read once at boot; the UI/tool validates and the gateway only checks syntax.

Please check against the code (stm32/firmware) and the main spec:
1. Is the protocol sound and complete: replies by broadcast so a PC in another subnet hears them; nonce lifetime and single use; HMAC string unambiguous; lockout; replay?
2. lwIP feasibility: receiving 255.255.255.255 on the Ethernet netif and sending the broadcast reply out of Ethernet when LTE is the default route (priority 2); socket vs raw API.
3. Store/restart integration: merging the 7 Ethernet fields into `network` with store_set_text, ack before restart, store_factory_reset.
4. Anything unsafe, contradictory, ambiguous or missing that would bite a technician using the tool; anything over-engineered to drop.

Reply with findings ranked (Critical / Important / Minor) with file:line where useful, then a verdict: APPROVED or CHANGES NEEDED.
