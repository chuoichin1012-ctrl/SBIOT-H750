Code review (read-only) of commit 7ca7fae on branch disc-noip (`git diff main..7ca7fae -- stm32`). The user (2026-10-17) asked why FinderTool could not find a gateway set to DHCP on a link with no DHCP server, and chose: the disc task answers while the Ethernet has no address.

- lwipopts.h: `#define LWIP_IP_ACCEPT_UDP_PORT(port) ((port) == PP_NTOHS(48600))` (ip4.c then lets a datagram to 48600 in while the netif has no address, as DHCP's port 68); disc.c: a `_Static_assert` ties it to DISC_PORT; handle() no longer returns when !st.bound; the reply goes by the netif-bound socket with source 0.0.0.0 (udp_sendto_if takes the netif address as the source; no ERR_RTE for an unspecified one), found carries eth 0.0.0.0.
- FinderTool: Found.HasAddress (false for 0.0.0.0); the table shows "no address (DHCP)", double-click does not open http://0.0.0.0 and points to Network…; C# test.
- Spec 2026-10-17-lan-discovery-design.md §2.4 and §5 updated.

Verified on the board (cabled to the PC, no DHCP server): the new bench case `disc_check.py noaddr` failed first on the old firmware (the board unreachable, recovered over the console with the whole network document), then passed: saved DHCP + restart → found with eth 0.0.0.0 → `set` the static address from FinderTool → found again, network identical. The full disc_check 19/19 after it (a 70 s flood of 4.0 M datagrams). dotnet test 33/33, build 0 warnings; app build, check_elf ok.

Please check: any other lwIP path that receives or replies wrongly with no address (e.g. a unicast from a host that knows our MAC, ARP for the reply, the flood path when unconfigured), anything that now answers on LTE or another netif, and the bench case's cleanup. Reply with findings ranked (Critical / Important / Minor), then APPROVED or CHANGES NEEDED.
