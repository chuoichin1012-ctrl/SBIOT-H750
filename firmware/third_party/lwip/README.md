# lwIP 2.2.1

TCP/IP stack of the app (spec §5.6, §7.1).

- **Source:** https://codeload.github.com/lwip-tcpip/lwip/tar.gz/refs/tags/STABLE-2_2_1_RELEASE, with SHA-256 `ce0b7461c0ad9602c376f0bf07c5eb7253b48c7bf66f011c6bf3e2a96731c539`.
- **Files kept, unchanged except for one patch:**
  - `src/{core,api,include}`;
  - `src/netif/ethernet.c`;
  - `src/apps/sntp` (Plan 06b);
  - `contrib/ports/freertos` (the FreeRTOS port);
  - `COPYING` (BSD-3-Clause).
- **Left out:** the other apps, the other netifs and PPP (Plan 09 adds `src/netif/ppp`), docs and tests.
- **Patch** `../patches/lwip-2.2.1-sbiot.patch`. It has two changes, each marked "SBIOT patch":
  1. **`contrib/ports/freertos/sys_arch.c`:** `sys_thread_new` calls `LWIP_FREERTOS_STATIC_THREAD` when `lwipopts.h` defines it.
     - Without the patch, the port creates its thread with `xTaskCreate`, which takes its stack from the heap.
     - The app's `tcpip` thread gets a static 4 KB stack in DTCM instead (spec §5.3, §6.1).
  2. **`src/core/ipv4/dhcp.c`:** `dhcp_handle_ack` hands the lease's whole DNS list (0, 1 or 2 servers) to `LWIP_HOOK_DHCP_DNS(netif, n, servers)`, once per accepted ACK, and does not write lwIP's servers itself (Plan 10 ruling 5: each link keeps its own DNS, and `netmgr` sets the chosen link's). Without the hook, lwIP's own behaviour.
     - Upstream, every ACK, renewals included, overwrites a manual DNS.
     - `dhcp_handle_ack` only runs for an ACK that was parsed in full and accepted.
     - Tested by `test/host/test_dhcp_dns.c`.
- **The port's settings:**
  - `app/lwipopts.h`;
  - `app/arch/cc.h`, which places the memory pools: memp pools in AXI, the `MEM` heap in SRAM1 (spec §5.6).
