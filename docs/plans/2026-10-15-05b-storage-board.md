# Plan 05b: storage on the board

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The storage of Plan 05a runs on the board: FlashDB over SPI4, the store mounted at boot, the log captured, and newlib allocating from the one heap. It is measured under load.

**Architecture:**
- **`components/board/spi_nor.c`** gains an optional port: `lock`/`unlock`, `busy_wait`, and `data` for long data phases. The bootloader passes none and keeps polling.
- **`app/flash_spi4.c`** is the app's one SPI4 object, used by both the OTA writer and FlashDB:
  - a mutex;
  - `vTaskDelay(1)` between status polls;
  - data phases of 256 bytes or more by DMA, through a 4 KB buffer in SRAM1. MPU region 2 makes it non-cacheable, so no cache maintenance is needed, only `__DMB()` on each side;
  - completion by semaphore, and an abort on timeout.
- **CubeMX** (`mkioc.py`): DMA1 streams 0 and 1 for SPI4 RX/TX, and NVIC at priority 6 for the streams and SPI4.
- **`app/store_port.c`**: a recursive mutex, `now_us` uptime, and the log through `blog`. FlashDB's own messages also go to `blog` on ARM.
- **`app/heap_newlib.c`**: heap_5 over 256 KB of AXI. `malloc`, `_malloc_r` and the rest are routed through `components/board/heap_wrap.c` (host-tested). `configUSE_NEWLIB_REENTRANT` is on, and `check_elf` gates that only one allocator is linked.
- **`components/syslog`**: every `blog` line goes to a queue (`blog_tap`). The syslog task keeps a 32 KB ring in SRAM4, and writes `W`/`E` lines to the log TSDB, at most 30 a minute. `SLOG_I/W/E` macros set the level.
- **The app** mounts the store in `init`. Configs are read once, at boot (spec §9.1, a user decision). Bench console commands: `S` status, `p` save, `G` 1000 saves, `P` idle counter, `W` warning, `l` the persistent log.

**Spec:** §5.4 (SPI4 row), §5.5, §9.1 (measurements, the boot-only config rule), §12.4 (the staging pitfall). **Roadmap:** row 05b.

## Bench findings

- **B1. A save took about 210 ms once cfg was full of old values.** FlashDB's sector cache defaults to 8 entries, against 16 sectors per KVDB, and every uncached sector is scanned KV by KV over SPI. With `FDB_SECTOR_CACHE_TABLE_SIZE` 16, 1000 saves take 12.2 s; the slowest, a save that collects garbage, takes about 250 ms.
- **B2. The mount took 2.8 s with DMA from 32 bytes.** FlashDB checks every KV's CRC in 32-byte reads, and DMA's setup and interrupt cost more than polling them. With DMA only from 256 bytes, the mount takes 1.8 s on a well-used database, and 0.2 s on a fresh one. Spec §9.1's 30 ms estimate was far too low, since a small read through HAL costs about 170 µs. Optimising this is left for later.
- **B3. The UART7 console starved every lower-priority task.** `HAL_UART_Receive` with a timeout spins, so syslog (priority 2) and idle ran almost never, and a warning stayed in the queue. The console now sleeps 10 ms while nothing has come in.
- **B4. Flashing QSPI over SWD while staging held another image:** the bootloader installed staging over it. This is noted in `flash.sh` and in §12.4: upload over UART7 instead.
- **B5. Bench timings grew with the app** (122 KB now, a boot of about 2.3 s). `boot_check` now spaces its software resets 3 s apart, and `ota_check` sizes its upload windows by the image.

## Measurements (`test/bench/store_check.py`, all pass)

- A config save takes 9–171 ms, and the config is there after a reset. The boot counter goes up by one per boot.
- 1000 saves in 12.2 s, none failed, the slowest 247 ms. The CPU was 71 % idle meanwhile, the wdog task's largest gap was 200 ms, DMA errors 0, and the lowest free heap was 247 696 bytes.
- 5 resets in the middle of the stress: every boot mounted (in 1.7–2.0 s), stayed writable, and read back a config that was saved.
- A warning reaches the persistent log and is there after a reset.
- Regression: `ota_check.py` and `boot_check.py` (with and without `--hostile`) pass. On the host, `test_store` (both passes) and `test_heap` pass.

## Rulings (for review)

1. **DMA only from 256 bytes.** The user proposed DMA for blocks that are large enough; the threshold is set by the measurement in B2. Cost if wrong: more CPU time on small reads.
2. **One mutex for all SPI4 users,** held for a single read, erase or program, never across a FlashDB transaction (the store's own lock covers that). Cost if wrong: an OTA upload and a save interleave at operation level, which is safe, since they use different regions.
3. **No real clock in the store yet** (`wall_ms` 0). Log lines carry the boot and the uptime until SNTP, in Plan 06.
4. **Console commands `G`, `P` and `W` are for the bench.** They go away with the console in Plan 06.
5. **The mount time (1.8 s worst seen) is accepted for now.** Cost if wrong: about 2 s more boot time after heavy saving.
6. **Configs are read once at boot** (user decision). The store's listeners have no live consumer, and are kept only until Plan 06 decides on the "restart required" flag.

## Codex round 1 (NOT APPROVED), and the fix

1. **FlashDB still used SPI4 after its init failed.** `flash_spi4()` now hands out a `nor_t` whose every operation fails when `spi_nor_init` failed, as the bootloader does since Plan 04b. `store_init` also answers `STORE_EFLASH` when the mount ends read-only: a mount over a failing flash used to report "mounted", with defaults and a wrong boot count, although nothing was written.
   - Bench case in `store_check.py`: `spi_nor_init` is forced to fail through the debugger (r0 at its return). The log shows `spi4 ... FAILED` and `store FAILED`.
   - The next boot finds the boot counter one up from before the forced boot, and the same config: no write happened in between.

## Tasks

- [x] SPI4 port and DMA (CubeMX streams, `flash_spi4.c`); OTA uses it. On the board: report, upload and install over DMA, DMA errors 0.
- [x] Heap: `test_heap` RED (no wrapper yet), then GREEN. The target wiring, plus the `check_elf` gate (no `__malloc_free_list`, `__malloc_sbrk_start` or `_sbrk_r`; `malloc` and `_malloc_r` are ours).
- [x] Store port and mount; syslog; console sleeping (B3).
- [x] `store_check.py` → `bench: ok`. Regression checks as above.
