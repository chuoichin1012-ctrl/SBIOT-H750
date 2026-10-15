# Plan 06a: Ethernet and lwIP on the board

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The board is on the LAN: the IP101GRI PHY, the H7 ETH driver, lwIP 2.2.1 with the pool table of spec §5.6, and the `network` config applied once at boot (static or DHCP, DNS, hostname). Verified by ping on the bench, under a flood, and across link drops.

**Architecture:**
- **lwIP 2.2.1** is vendored and pinned in `third_party/lwip`: `src/` and `contrib/ports/freertos`. One patch file (`patches/lwip-2.2.1-sbiot.patch`) has two changes, each marked "SBIOT patch":
  1. `sys_thread_new` calls `LWIP_FREERTOS_STATIC_THREAD` when defined, so that the `tcpip` stack is static in DTCM (spec §5.3). The upstream port would put it on the heap through `xTaskCreate`.
  2. In `dhcp_handle_ack`, the DNS loop runs only when `LWIP_DHCP_DNS_FROM_LEASE(netif)` is true (default 1). That function runs only for an ACK that lwIP has already parsed in full and accepted, so a malformed packet never touches the DNS servers.
- **`app/lwipopts.h`** holds the pool table (below). `LWIP_DECLARE_MEMORY_ALIGNED` puts every memp pool in `.axi_bss`, and `LWIP_RAM_HEAP_POINTER` puts the 64 KB `MEM` heap in `.sram1_bss`.
- **`components/netmgr/ip101.{h,c}`** is the PHY driver, written after ST's template `Drivers/BSP/Components/lan8742` (same object and IO-context shape: `Init/DeInit/ReadReg/WriteReg/GetTick`, status codes per link state). It differs from the template in three ways:
  - The address is found by scanning 0–31 for a valid ID in registers 2 and 3. The IP101 has no `SMR` register.
  - Speed and duplex come from the standard registers (`BMCR`, then `BMSR` autoneg complete, then `ANAR ∧ ANLPAR`), not a vendor register.
  - It can power the PHY down and up (`BMCR.PD`), for the bench.
  - It is host-tested against a fake MDIO register file.
- **`components/netmgr/eth_drv.c`** is the ethernetif, after ST's template `Projects/NUCLEO-H743ZI/Applications/LwIP/LwIP_HTTP_Server_Netconn_RTOS/Src/ethernetif.c`, changed to fit spec §5.4 and §6.1:
  - **TX copies:** `low_level_output` copies the pbuf chain into one of 4 TX frame buffers in SRAM1. A buffer is reused only after `HAL_ETH_ReleaseTxPacket` has handed it back through `HAL_ETH_TxFreeCallback`. When none is free within 20 ms, it answers `ERR_MEM`.
  - **RX is zero-copy** from 16 buffers of 1536 B in SRAM1, kept on our own free list (not `LWIP_MEMPOOL`, so that the pool can sit in SRAM1 while the other memp pools sit in AXI). The custom pbuf free returns a buffer and, if the HAL had run out (`RX_ALLOC_ERROR`), wakes `eth_rx` so that it rebuilds the descriptors. Without that wake, RX stays dead after a flood.
  - **The `eth_rx` task** (2 KB static stack, priority 22) waits for the ETH interrupt's semaphore, at most 1 s. On each wake it hands at most 8 frames to lwIP; if more are waiting, it calls `vTaskDelay(1)` (spec §6.1). Each 1 s timeout, it polls the PHY link (spec §7.1). On a link change it sets the MAC's speed and duplex, starts or stops the ETH, and calls `netif_set_link_up/down` through `tcpip_callback`.
  - **Init order** (Codex r1):
    1. hold `ETH_RESET` (PD13) low for 10 ms, release it, wait 50 ms. The PHY then drives `REF_CLK`, which the ETH's DMA reset needs;
    2. `HAL_ETH_Init`, with our MAC, our descriptors in `.sram1_bss`, and `RxBuffLen` 1536. This enables the ETH clocks and GPIO, and sets the MDIO clock divider;
    3. scan for the PHY over MDIO.

    Any failure is logged and the network stays down; there is no `Error_Handler` loop, the same pattern as the dead `nor_t` of 05b.
  - **MAC filter:** pass all multicast, so that IGMP (the bridge, later) receives its groups.
  - **Checksums:** in software (`CHECKSUM_GEN_*`, `CHECKSUM_CHECK_*` 1), and the TX config does no insertion. One rule in one place.
  - Hot paths in ITCM: `ETH_IRQHandler` and `HAL_ETH_IRQHandler`.
- **`components/netmgr/netcfg.{h,c}`**, pure C and host-tested:
  - It turns the `network` and `system` configs into a `net_cfg_t`: static or DHCP; ip, mask and gateway; manual or automatic DNS; dns1, dns2; hostname.
  - The ESP32 rules carry over: `ipMode` 1 means static. An incomplete static address falls back to DHCP, and DNS then follows `dnsMode`. A static address always uses the manual DNS.
  - Addresses are parsed strictly: dotted quad, 0–255, no leading `+` or spaces.
  - It also derives the MAC from the UID: `02` (locally administered, unicast) followed by the first 5 bytes of SHA-256 over the 12-byte UID. The same board keeps the same MAC.
- **`components/netmgr/netmgr.{h,c}`** runs in `init`, after `syslog`. It reads both configs **once** (spec §9.1; a change applies after a restart), then calls `tcpip_init(done, ...)`, and waits on a semaphore that `done` gives.
  - `done` runs in the `tcpip` thread (Codex r1: these calls need the core lock). In this order (Codex r2: `dhcp_start` needs the netif up), each result checked: `netif_add`, then the static address and the manual DNS servers, then `netif_set_up`, then `dhcp_start` when DHCP.
  - The ETH link changes reach lwIP through `tcpip_callback`.
  - `netmgr_status()` copies the **running** values (link, speed, ip, mask, gateway, DNS, MAC, PHY address and ID) and the pool usage from `MEMP_STATS`, all under `LOCK_TCPIP_CORE()`.
  - **DNS with DHCP:** `LWIP_DHCP_DNS_FROM_LEASE` is `!dns_manual` (patch 2). Automatic DNS takes the servers of every accepted ACK, renewals included. A manual DNS is never overwritten.
- **RNG** for `LWIP_RAND` (DHCP xid, DNS id and port, local ports), and later for mbedTLS (spec §7.3): the RNG IP in `app.ioc` with HSI48 as its kernel clock. HSI48 is switched on in `HAL_RNG_MspInit` with `__HAL_RCC_HSI48_ENABLE()` and a bounded wait, because `HAL_RCC_OscConfig` is forbidden in the app (`check_elf`). It offers two functions (Codex r1):
  - `board_rng(buf, len)` answers an error when the hardware RNG fails, after resetting a seed error once. It is the only source of entropy that mbedTLS will be given (spec §7.3).
  - `board_rand32()` is for `LWIP_RAND` only. It falls back to a xorshift seeded from the UID and `now_us`, with one log line. Those values are not secret; they only keep DHCP and the ports working.
- **Console** (bench, UART7):
  - `n`: network status;
  - `N`: read one line of JSON and save it as `network` (applies after a restart);
  - `L`: power the PHY down for 3 s, then up.

**Spec:** §5.3, §5.4 (ETH rows), §5.6, §6.1 (`tcpip`, `eth_rx`), §7.1, §9.1 (configs at boot). **Roadmap:** row 06 is split into 06a (this plan) and 06b (Mongoose, `/api/*`, UI, clock).

## Pool table (spec §5.6; sizes measured from the link map go into §5.6 after the build)

| Item | Value | Region | Why |
|---|---|---|---|
| `MEM_SIZE` | 64 KB | SRAM1 | spec §5.6; TX data of `tcp_write` and `PBUF_RAM` |
| ETH RX buffers | 16 × 1536 B | SRAM1 | zero-copy RX, more than the 4 descriptors + IP reassembly (`IP_REASS_MAX_PBUFS` 10) |
| ETH TX buffers | 4 × 1536 B | SRAM1 | one per TX descriptor, TX copies |
| ETH descriptors | 4 RX + 4 TX | SRAM1 | CubeMX `ETH_RX/TX_DESC_CNT` |
| `MEMP_NUM_NETCONN` | 64 | AXI | spec §5.6 |
| `MEMP_NUM_TCP_PCB` | 56 | AXI | 2 × 16 server clients, about 8 web, 4 WebSocket, MQTT, 5 Modbus TCP, 2 bridge clients, plus TIME_WAIT slack |
| `MEMP_NUM_TCP_PCB_LISTEN` | 8 | AXI | 2 TCP servers, 2 Mongoose, Modbus TCP gateway, spare |
| `MEMP_NUM_UDP_PCB` | 12 | AXI | DHCP, DNS, SNTP, 2 bridge UDP, spare |
| `MEMP_NUM_TCP_SEG` | 128 | AXI | queued segments across all PCBs (16 per PCB, by `TCP_SND_QUEUELEN`) |
| `MEMP_NUM_NETBUF` | 16 | AXI | UDP receive |
| `MEMP_NUM_PBUF` | 32 | AXI | `PBUF_REF`/`ROM` headers |
| `MEMP_NUM_TCPIP_MSG_API` / `_INPKT` | 16 / 32 | AXI | |
| `PBUF_POOL_SIZE` × `PBUF_POOL_BUFSIZE` | 16 × 512 B | AXI | PPPoS input only (Plan 09); ETH RX has its own pool |
| `ARP_TABLE_SIZE`, `MEMP_NUM_ARP_QUEUE` | 16, 16 | AXI | |
| `MEMP_NUM_IGMP_GROUP` | 8 | AXI | `LWIP_IGMP` on |
| `DNS_TABLE_SIZE`, `DNS_MAX_SERVERS` | 4, 2 | AXI | |
| `TCP_MSS`, `TCP_WND` = `TCP_SND_BUF` | 1460, 4 × MSS | | spec §5.6 |
| `TCPIP_MBOX_SIZE` | 32 | heap | FreeRTOS queue (32 × 4 B) |
| netconn recv/accept mbox | 8 each | heap | at most 64 netconn × about 112 B ≈ 7 KB, inside the "small allocations" of §5.3 |
| `LWIP_SOCKET_OFFSET`, max sockets | 0, 64 | | newlib's `FD_SETSIZE` 64 ≥ offset + netconn (lwIP `#error`s otherwise; a compile unit checks it with `sys/select.h` included first, as Mongoose will) |
| `tcpip` stack | 4 KB static | DTCM | spec §6.1, through the patch |
| `eth_rx` stack | 2 KB static | DTCM | spec §6.1 |
| PPP | off until Plan 09 | | |

**SRAM1 budget after 06a:** 64 + 24.6 + 6.1 + 0.3 + 4 (the SPI4 buffer) ≈ 99 KB of 128 KB. That leaves about 29 KB for the USART rings and TX buffers of Plan 07 (2 + 2 + 4 RX and 3 × 2 TX, 14 KB).

**Change to spec §5.3:**
- SRAM1 keeps only what a DMA engine touches, plus `MEM`.
- The memp pools and `PBUF_POOL` are CPU-only, so they move to AXI, about 30 KB of AXI's 63 KB that is not yet allocated. Cacheable memory is also faster for the CPU.

## Bench (`test/bench/net_check.py`)

The bench PC's Ethernet port is fixed at 192.168.0.10/24, with no DHCP server on the link. WSL2 runs in NAT mode, so pings go through Windows (`ping.exe`), and the flood runs from Windows Python (`/mnt/c/Python311/python.exe`).

1. **Factory default (static 192.168.0.7):**
   - the link comes up at 100 Mbit full duplex;
   - 20 pings answer;
   - a 3000-byte ping (fragmented, reassembled by lwIP) answers;
   - the PHY address and ID are logged.
2. **Static from config:** `N {"ipMode":1,"ip":"192.168.0.77",...}`, then a reset.
   - .77 answers and .7 does not.
   - `n` shows the running values.
   - Then the factory default is restored.
3. **Link drops:** `L` five times.
   - Each time the link goes down and comes back, and pings answer again.
   - RX buffers in use return to the baseline.
4. **Flood:** 20 s of UDP to a closed port at full speed from Windows.
   - During it: the wdog gap is < 400 ms, the console still answers, and the CPU still idles.
   - After it: pings answer within 2 s, and RX buffers in use return to the baseline.
5. **DHCP (`--dhcp`, only when the board is on a LAN with a DHCP server):**
   - `N {"ipMode":0,...}`, then a reset;
   - an address is leased and answers pings;
   - with `dnsMode` 1, the manual DNS is kept after the ACK.

Regression: `store_check.py`, `ota_check.py`.

## Bench findings

- **B1. The polled UART7 console went deaf.**
  - The tasks above `init` (`tcpip`, `eth_rx`) can hold the CPU for longer than one character at 115200 baud (87 µs): an MDIO link poll spins about 130 µs. The RX register then overran.
  - Once `ORE` is set, `RXNE` never comes back: the console answered nothing, and an upload stopped at byte 0.
  - **Fix:**
    - UART7 in the app gets its 16-byte RX FIFO (`mkioc.py`; the bootloader is unchanged);
    - the console clears `ORE`;
    - the bench sends a line after `N` once the console listens, since it polls every 10 ms.

  The serial driver of Plan 07 (DMA, spec §6.5) replaces this console path.
- **B2. The bench PC renegotiates its link.** In its first autonegotiation, its Intel I219 advertises only 10 Mbit full duplex (`ANLPAR` 4061). About a second later it offers everything (cde1). The board follows: the link comes up at 10, drops, then comes up at 100 full about 8 s after reset. The log line now shows the partner's abilities.
- **B3. The IP101's MDIO address changes between boots,** 1 on some and 0 on others; 0 is also its broadcast address. The scan now tries 1–31 first, then 0 (`test_ip101`). **For the user:** check the PHYAD strap pins on the board, which seem not to hold a fixed level at reset.
- **B4. `MX_RNG_Init` is not called,** so `hrng.Instance` was NULL and `HAL_RNG_Init` faulted on the null guard (MemManage, `MMFAR` 0), in a watchdog reset loop. `board_rng_init` now fills the handle itself, as `eth_drv` does for `heth`.
  - Recovery on the bench, since the broken image was in staging: the same image was rebuilt for its symbols. Through the debugger, `board_rng_init` and `netmgr_init` were made to return at once, then the fix was uploaded over UART7.
- **B5. Plan 05b's commit left out `boot.c`'s new `spi_nor_init` argument,** so the bootloader on `main` did not build. It is fixed in its own commit, 8e0d626.

## Measurements (`test/bench/net_check.py`, all pass)

- Factory default 192.168.0.7: the link at 100 Mbit full duplex, 20 of 20 pings answered, and 4 of 4 pings of 3000 bytes, fragmented. The PHY ID is 02430c54.
- A static address from the config after a restart (.77): it answers, .7 does not, and `n` shows the running values.
- Five link drops (`BMCR.PD` for 3 s): each comes back at 100 Mbit and pings answer. The RX buffers in use return to 4, one per descriptor.
- A 20 s UDP flood to a closed port: about 228 000 datagrams of 1000 B, about 11 400/s, each costing an ICMP reply.
  - During it: the wdog gap stays at 200 ms, the console answers, the CPU is about 10 % idle.
  - After it: pings answer at once, RX buffers return to 4, `rx_starved` 0, `tx_busy_drops` 0.
- Memory:
  - SRAM1 101 120 B of 128 KB;
  - AXI 295 392 B, of which the lwIP pools take 26 351;
  - DTCM 64 404 B with the 4 KB MSP;
  - image 179 KB.
- Regressions:
  - `store_check.py` passes: 1000 saves, CPU 71 % idle, lowest free heap 247 328 B, against 247 696 in 05b (lwIP's semaphores and mailboxes);
  - `ota_check.py` passes (install, resets during installs, cut uploads, Reload, clone, wrong key).
  - Host: `test_ip101`, `test_netcfg`, `test_dhcp_dns`, `test_rng`.

## Rulings (for review)

1. **Link changes take the lwIP core lock in `eth_rx`** rather than going through `tcpip_callback`. That is the same serialisation, done synchronously, and the ETH start and stop never run beside `low_level_output`. Cost if wrong: none seen.
2. **CubeMX's `eth.c` is compiled for its `HAL_ETH_MspInit`, handle and descriptors.** `MX_ETH_Init` and `MX_RNG_Init` are never called: both end in `Error_Handler`, a spin with interrupts off. `check_elf` gates that neither is linked.
3. **A DHCP lease could not be tested on the bench** (no server on the direct link). `test_dhcp_dns` covers lwIP's real client on the host; `net_check.py --dhcp` runs once the board is on a LAN with a server.
4. **The 4 RX descriptors stay** (CubeMX default): the flood found no starvation and no lost watchdog period.
5. For 06b: spec §6.4 still mentions closing every client when the password changes (F19). Under the restart rule (§10.1), 06b removes it.

## Tasks

- [x] **T1. lwIP vendored** (SHA-256 of the tarball in the README) plus the `sys_thread_new` patch; `lwipopts.h`, `arch/cc.h` and the placement macros; the `FD_SETSIZE` compile unit; CMake. Spec §5.3/§5.6 updated with the table.
- [x] **T2. `ip101`:**
  1. `test_ip101` RED, then GREEN, covering: the address scan (none found, found at 1, a read error); link down (BMSR latched: read twice); autoneg not done; 100 full, 100 half, 10 full; autoneg off (forced from BMCR); power down and up.
- [x] **T3b. DHCP and DNS on the host** (Codex r1). `test_dhcp_dns` builds lwIP's real DHCP client (`NO_SYS` 1, patch 2 applied) and feeds it DHCP packets it generates. The cases:
  - automatic DNS: the servers of the ACK are applied;
  - a renewal ACK with other servers: applied;
  - an ACK whose option 6 is followed by a malformed option: rejected, and the servers stay as they were;
  - manual DNS: neither an ACK nor a renewal changes it.

  RED: with `LWIP_DHCP_DNS_FROM_LEASE` stubbed to 1, the manual case fails.
- [x] **T3. `netcfg`:**
  1. `test_netcfg` RED, then GREEN, covering:
     - static complete; static with a bad mask, which falls back to DHCP with DNS by `dnsMode`;
     - DHCP with auto or manual DNS;
     - strict parsing (`256.1.1.1`, `1.2.3`, ` 1.2.3.4`, `1.2.3.4x`, empty);
     - a hostname that is too long or empty (default `sbiot-gw`);
     - missing objects (defaults);
     - the MAC from the UID: fixed vector, bit 1 set, bit 0 clear.
- [x] **T4. Board:**
  - `mkioc.py`: ETH NVIC at priority 6 and RNG with HSI48; regenerate.
  - `app.ld` maps `.RxDescripSection` and `.TxDescripSection` into `.sram1_bss`, and puts the ETH ISRs in ITCM.
  - `eth_drv.c`, `netmgr.c`, `board_rand32`; console `n`, `N`, `L`.
  - `check_elf` gates:
    - the descriptors, RX and TX buffers and `ram_heap` are in SRAM1;
    - every `memp_memory_*_base` is in AXI;
    - `ETH_IRQHandler` is in ITCM;
    - the DTCM budget holds.
- [x] **T5. `net_check.py`** passes cases 1–4 (5 when a DHCP LAN is available); regression as above; measurements recorded here.

## Codex round 1 (NOT APPROVED), and the fixes

1. PHY scan after `HAL_ETH_Init`, since the HAL enables the clocks and sets the MDIO divider (Init order above).
2. lwIP is set up inside the `tcpip_init` callback, and the status is read under the core lock.
3. The DNS servers come only from an ACK lwIP has fully accepted. The parse hook is dropped in favour of patch 2, and T3b tests it on the host.
4. The xorshift fallback serves `LWIP_RAND` only. `board_rng` fails when the hardware RNG fails, and it is the only source for mbedTLS.

## Codex round 2 (NOT APPROVED), and the fix

1. `netif_set_up` comes before `dhcp_start`, which needs the netif up; every result is checked.

## Code review round 1 (NOT APPROVED), and the fixes

1. **A speed or duplex change between two polls was missed.** `ip101_action(applied, now)` (`test_ip101`, RED first) decides:
   - start, stop, keep;
   - **reconfigure**, for still up at another speed or duplex, such as 10 → 100 with no "down" seen in between;
   - a read error keeps the state.

   `eth_drv` stops the MAC, sets it again and starts it, all under the core lock, and the netif stays up. The log says `link now`.
2. **`HAL_OK` from the RNG does not prove a good word on the H750:** the HAL checks the seed error only where `RNG_CR_CONDRST` exists.
   - `components/board/rng_core.c` reads the registers itself: a clock error fails the word, and a seed error is recovered once as RM0433 §34.3.7 says (clear SEIS, drop 12 words, SEIS must stay clear).
   - A word read while, or just before, an error shows is dropped.
   - `board_rng` writes nothing more into the buffer once a word fails.
   - `test_rng` (RED first) covers: a good word; a seed error that recovers; one that stays; an error with DRDY; a clock error; DRDY never.

`net_check.py` passes again.

## Code review round 2 (NOT APPROVED), and the fix

1. **The seed recovery waited for DRDY before each of its 12 reads,** which a stopped RNG never raises. Now it reads and drops them without waiting, as RM0433 §34.3.7 says; only the word that is kept waits for DRDY. A new `test_rng` case keeps DRDY low until 12 reads. It failed first, at 0 reads.
   - Fixed in the fake as well: clearing SEIS now also clears its repeated last status.
   - On the board: the image boots and answers pings, with no "RNG FAILED".
