# SBIOT Finder

A Windows tool that finds SBIOT gateways on the LAN, also ones in another subnet. It shows each gateway's MAC, SN, IP and firmware, and opens its Web UI. It can also change a gateway's Ethernet settings, restart it, or factory-reset it. It works like the configuration utility of the USR-M100.

Protocol and design: `stm32/docs/specs/2026-10-17-lan-discovery-design.md`. Gateway side: `components/services/disc.c` (UDP 48600). Plan 12.

## Use

1. Run `SbiotFinder.exe`. It is one self-contained file; nothing needs to be installed. On the first run, Windows asks whether to allow it through the firewall: allow it on private networks.
2. Pick the network adapter that is on the gateways' LAN, then press **Search**. Every gateway on that switch answers within 2 s.
3. To act on a gateway, select its row:
   - Double-click the row to open the gateway's Web UI.
   - **Network…** sets the gateway's address: DHCP or static IP, subnet mask, gateway (`0.0.0.0` for none) and DNS.
   - **Restart** restarts the gateway.
   - **Factory reset** erases every setting of the gateway.

   After a change, the tool waits up to 2 minutes for the gateway to restart, then updates its row.

Limits:

- Every packet is a broadcast, so the PC and the gateways must share a switch. Broadcasts do not cross routers.
- There is no password (the user's decision, 2026-10-17). Anyone on the LAN can change a gateway's address, restart it or factory-reset it.
- A gateway set to DHCP on a LAN without a DHCP server has no address and does not answer. Press its Reload button to restore the factory settings (static `192.168.0.7`).

## Build

You need the .NET 10 SDK. It runs on Linux/WSL too: `EnableWindowsTargeting` fetches the Windows Desktop packs, so the exe builds there, but it only runs on Windows.

```bash
cd stm32/tools/finder
dotnet test                       # the protocol tests; disc_vectors.json is shared with the C tests
dotnet publish Sbiot.Finder -c Release -r win-x64 --self-contained -p:PublishSingleFile=true -o out
```

`out/SbiotFinder.exe` (about 50 MB, compressed) is all you need to ship.

The icon and the header logo are in `Sbiot.Finder/Resources`: `make_icon.py` builds them from `assets/app-icons/sbiot` and from `docs/SBIOT.ai` (page 2, the full-colour logo). Run it again only when the logo changes.
