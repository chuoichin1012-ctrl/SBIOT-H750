# Plan 11a: MAC and serial number at programming, the factory calibration tool, the key audit

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (Native, chosen) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** a supervisor restarts the gateway when one of its tasks stops; LED WORK tells normal running, a firmware transfer or install, and a halt apart; the programming tool writes each board's MAC and serial number into its signed binding record; the firmware reads them from there and refuses to run without them; a production tool writes the ADC calibration; `audit_keys.py` reports 0 unread config keys.

**Architecture:** The binding record (spec §11.5) grows to a version 2 carrying the MAC and the SN under the same Ed25519 signature, so neither can be changed without the private key. `provision.py` computes the MAC from the UID (today's formula, so a board keeps its MAC) and takes the SN from a counter file on the production PC. The bootloader and the app accept only version 2; an older record halts without erasing. Plan 11 of the roadmap is split: 11a is this (firmware and tools, host-tested), 11b the bench HIL runs.

**Tech Stack:** C11 (bootloader and app, Monocypher Ed25519), Python 3 (`provision.py`, `cryptography`), STM32CubeProgrammer over SWD, host tests in `stm32/firmware/test/host`.

**Spec:** `stm32/docs/specs/2026-10-14-stm32-firmware-design.md` (§11.5 anti-clone, §6.6 and §12.4 factory record, §12.2 gates); roadmap `stm32/docs/plans/2026-10-14-00-roadmap.md` row 11 and its "Proposed for Plan 11".

## Global Constraints

- The user, 2026-10-17:
  - MAC: locally administered, `02` + SHA-256(UID)[0..4], computed by the tool;
  - SN: `SB` + YYMM + a 6-digit sequence number, e.g. `SB2610000123`;
  - a board whose record lacks the MAC and SN stops with an error (no fallback to the UID).
- The private key never leaves the production PC; the bootloader never writes a record (spec §11.5).
- Keep it simple: no new machinery beyond the record, the counter file, two tools.
- The user, 2026-10-17: LED WORK (it is WDI, PC10, SGM706 through Q6) blinks with a 1 s period when running, 0.2 s during an OTA (receiving an image), an install or a factory restore, 0.5 s when halted. Every toggle stays well inside the SGM706's minimum timeout of 1.0 s; IWDG1 is reloaded at every kick as now.
- Run only the affected tests after an edit. Do not run LTE cases that re-attach the modem (the SIM was locked by the Plan 10b bench).

## Review Focus

1. A record copied from another board, or one with its MAC or SN changed and the CRC fixed, is a clone (erase, halt), as with version 1.
2. A version 1 record (every board programmed before this plan) halts the bootloader with a message and erases nothing; `flash.sh bind` then makes it run again.
3. Two boards programmed one after the other never get the same SN, also when a programming fails half way.
4. Re-binding the same board keeps its MAC (the same formula over the same UID), so its LAN address reservations still hold.
5. The SN in `/api/status`, the registration and heartbeat packets, Edge's `sys_sn` and the console all come from the record.

---

### Task 0: LED WORK rhythm (the user)

**Files:** `firmware/components/board/include/board.h` and `board_io.c` (`board_iwdg_reload()`, `board_wdi_toggle()`; `board_kick()` keeps doing both for the SWD loader and early boot), new `components/board/pace.c/.h` (pure: `#define PACE_RUN_MS 500u`, `PACE_BUSY_MS 100u`, `PACE_HALT_MS 250u`; `bool pace_due(uint32_t now_ms, uint32_t *last_ms, uint32_t half_ms)`), `components/board/wdog.c` (loop every 50 ms (Codex plan r1: a 100 ms loop cannot make 250 ms toggles): IWDG1 reloaded each loop, WDI toggled when `pace_due` with the mode's half period; two gaps measured apart, the loop's (IWDG1) and the toggles' (WDI); `void wdog_set_busy(bool)`, `void wdog_set_halt(void)`), `boot/boot.c` (a paced `boot_kick`: IWDG1 every call, WDI by `pace_due` on `HAL_GetTick`; 100 ms while it checks, installs or restores, 500 ms while it samples Reload, 250 ms in `halt()` and the no-image halt), `components/webapi/web.c` and `app/bench_ota.c` (busy from the upload's claim to its end), `app/app_main.c` (the bind halt calls `wdog_set_halt`); test `test/host/test_board.c` (or a new `test_pace.c`).

- [ ] Test: `pace_due` is false before the half period and true at it, updates `last` only when due, survives the `uint32_t` wrap; calls every **50 ms** (the wdog loop) give toggles exactly 500, 100 and 250 ms apart; calls at the bootloader's irregular pace (every 10 ms, and a 300 ms gap) never toggle closer than the half period nor later than half + one call.
- [ ] Fix as above. `/api/status` reports both gaps: `iwdgMaxGapMs` (the loop, nominal 50 ms; the 400 ms criteria of §6.1 and §12.2 now apply to it) and `wdiMaxGapMs` (the toggles, nominal 500 ms; criterion < 600 ms, inside the SGM706's 1.0 s minimum).
- [ ] Build boot and app; board: the LED blinks at 1 s when running, 0.2 s during a web OTA upload (and the install after it), 0.5 s halted (an unbound board); `iwdgMaxGapMs` < 400 ms and `wdiMaxGapMs` < 600 ms after the bench suites.
- [ ] Spec: §6.1 (`wdog` row), §11.2 and §11.5 (the LED patterns). Commit.

### Task 0b: a supervisor of the other tasks (the user)

The wdog task, at priority 31, kicks even while another task hangs (`tcpip` stuck, `web` frozen, `mqtt_io` locked): today only a dead scheduler or a late `init` resets the board. The user (2026-10-17): a task that watches the others. Heartbeats, not an idle-task kick (idle legitimately starves during a TLS handshake, a FlashDB GC, a LAN flood).

**Files:** new `firmware/components/board/hb.c/.h` (pure; FreeRTOS-free), `components/board/wdog.c` (checks every 1 s), the supervised tasks (one `hb_beat` per loop; any wait without an end gets one), `app/app_main.c` (console `I`: the bench hang), spec §6.1 and §11.4; test `test/host/test_board.c`.

- `int hb_add(const char *name, uint32_t timeout_ms, uint32_t now_ms)`, `void hb_beat(int id, uint32_t now_ms)`, `int hb_expired(uint32_t now_ms)` (the first slot whose last beat is older than its timeout, else -1; wrap-safe), `const char *hb_name(int id)`.
- **Concurrency** (Codex plan r4): tasks register while the scheduler runs and the wdog scans. `hb_add` runs inside a critical section (`wdog_hb_add()` in `wdog.c` wraps it with `taskENTER_CRITICAL`); the slot is filled (its name **copied** into `char name[16]`: some task names are built on a stack, `bridge%d`; its timeout; its first beat) before the count that publishes it is stored (`__atomic_store_n`, release); `hb_beat` and the scan use `__atomic_store_n`/`__atomic_load_n` on the timestamp. `HB_MAX` is 32, above the 18 tasks that can run at once; a full table is never silent: `hb_add` returns -1, the caller logs `hb: no slot for <name>`, and `/api/status` counts it in `hbMissing` (the bench checks 0).
- **Supervised, with the timeout each gets** (generous: a reset must never come from a slow but working task):

  | Task | Timeout | Its beat |
  |---|---|---|
  | `tcpip` | 10 s | a `sys_timeout` every 1 s in the tcpip thread |
  | `eth_rx` | 10 s | its loop (its wait capped at 1 s) |
  | `web`, `console` | 30 s | each `mg_mgr_poll` round |
  | `bridge1`, `bridge2` | 60 s | their loops |
  | `mqtt`, `mqtt_io` | 120 s | their loops (a TLS handshake, a broker that hangs) |
  | Edge poll tasks, `edge_ctl`, `edge_rpt` | 120 s | their loops |
  | `lte`, `cmux_rx` | 180 s, 10 s | `lte`: `wait_events` and the AT waits (up to 60 s); `cmux_rx`: its loop |
  | serial RX tasks, `io`, `syslog` | 10 s, 10 s, 60 s | their loops |

  Tasks that do not run (a service off at boot) never register: nothing to watch.
- **A wait that is work, and a wait that is stuck** (Codex plan r4):
  - waiting for data (a queue, a message buffer, a notification, a socket) and sleeping on a schedule (MQTT's `retryInterval` up to 65535 s, Edge's report interval, a serial line with no traffic) are cut into slices of at most a third of the task's timeout, beating between slices, with the business deadline kept as it was (the remaining time carried over);
  - waiting for a lock (a mutex or a semaphore that guards shared state: `mqttgw.c` `lock_wait()`, the Edge cache, the serial bus) never beats: a lock that makes no progress past the timeout trips the supervisor, which is the point.
- On an expired slot (Codex plan r4: the path must not block; `blog` takes a mutex with no end, and syslog may be the task that hangs): the wdog writes the reason, `{magic, the slot's name, its seconds late, CRC-32}`, into a `.noinit` section in SRAM4 that neither the bootloader nor the app's startup clears (checked in `boot.ld` and `startup_app.c`), then stops kicking at once: IWDG1 resets the board about 1.6 s later. At the next boot, `init` finds a valid reason (and RCC_RSR's IWDG1 flag), logs `"wdog: <name> stopped for <n> s: restarted"` through the normal log, and clears it. No other recovery (keep it simple).
  - **The cache** (Codex plan r5): the app runs with the D-cache on, and the MPU leaves SRAM4 cacheable but for the ADC buffer. The reason is a 32-byte object aligned on a 32-byte cache line (`__attribute__((aligned(32), section(".noinit")))`, alone in its line); every write and the clear are followed by `SCB_CleanDCache_by_Addr` on that line and `__DSB()`; the boot reads it after `SCB_InvalidateDCache_by_Addr`. `check_elf.py` gains a gate: `.noinit` is NOLOAD, 32-aligned, inside SRAM4, outside every range the bootloader's linker script uses, and `startup_app.c` zeroes nothing in it.
  - **The order at the boot** (Codex plan r5): `app_main` copies `RCC->RSR` into a static before anything clears it (`report_boot` clears it today); after `store_init` has mounted, `init` decides with `hb_restart_was_ours(por, bor, iwdg, pin)` (pure, tested; Codex plan r6: the flags do not exclude each other, a power-on sets `PINRSTF` too): a power-on or brown-out flag → the reason is stale RAM, nothing logged, even with `PINRSTF`; otherwise a valid reason with `IWDG1RSTF` or `PINRSTF` (with R20 fitted the SGM706 pulls NRST first) → the log line; in every case the reason is cleared (clean and DSB).

- [ ] Test (host): `hb_expired` is -1 while every slot beats within its timeout; the slot past its timeout is returned, the first of two; a slot that beats again is no longer expired; wrap-safe across `UINT32_MAX`; `hb_add` past `HB_MAX` returns -1 and changes nothing; a name built in a buffer that is then overwritten stays as given; the reset reason encodes, decodes, and is refused with a wrong magic or CRC (random RAM after a power-up).
- [ ] Fix as above; every supervised task's waits end within a third of its timeout.
- [ ] Board:
  - console `I` makes the `web` task stop for 40 s: the board resets about 31–33 s after it, and the log after the boot holds `wdog: web stopped ... restarted`;
  - a valid long wait does not trip it: MQTT on with `retryInterval` 300 and an unreachable broker, 7 minutes, no reset;
  - a power-up (not an IWDG1 reset) logs no reason; `hbMissing` is 0; the bench suites never trip it;
  - `check_elf.py` passes its `.noinit` gate.
- [ ] Commit.

### Task 1: binding record version 2 (C and Python)

**Files:** `firmware/components/ota/include/bind.h`, `components/ota/bind.c`, `tools/provision.py`, `tools/test_provision.py`; tests `test/host/test_bind.c` (and its Python-signed fixtures, `bind_sample.bin` in the host Makefile).

**The record (little endian), 104 bytes at QSPI 0x3F0000:**

| Offset | Content |
|---|---|
| 0 | magic `SBB2` (4 B) |
| 4 | UID as read at 0x1FF1E800 (12 B), for people reading it |
| 16 | MAC (6 B): `02` + SHA-256(UID)[0..4] |
| 22 | SN (12 B ASCII, no NUL): `SB` + YYMM + 6 digits |
| 34 | reserved, 0 (2 B) |
| 36 | Ed25519 signature (64 B) over `"SBIOT-H750 bind2"` ‖ UID ‖ MAC ‖ SN, bytes 4–33 of the record |
| 100 | CRC-32 of bytes 0–99 |

- [ ] The UID rule (Codex plan r1): the signature covers the record's own UID, MAC and SN, and the record's UID must equal the chip's; a record whose UID is not this chip's is `BIND_CLONE` (another board's, valid or not).
- [ ] Test (C, host): a Python-signed v2 record verifies; a changed MAC or SN with the CRC fixed is `BIND_CLONE` (the signature fails); another chip's valid record is `BIND_CLONE` (the UID differs); a record whose UID was changed to this chip's with the CRC fixed is `BIND_CLONE` (the signature fails); a v1 record (`SBBD`) is the new `BIND_EOLD` and erases nothing; `bind_identity(rec, mac, sn)` returns the record's MAC and SN; an SN that is not `SB` + 10 digits fails the parse. Run: FAIL.
- [ ] Test (Python): `provision.py record KEY UID SN OUT` writes the layout above; the MAC equals `netcfg_mac`'s (a fixed UID vector shared with the C test); a bad SN is refused.
- [ ] Fix: `BIND_LEN` 104, `BIND_EOLD`, `bind_valid` over the v2 message, `bool bind_identity(const uint8_t rec[BIND_LEN], uint8_t mac[6], char sn[13])`. `provision.py`: `record KEY UID SN OUT` (the MAC computed inside), `next-sn COUNTER` (task 2).
- [ ] Run `test_bind`, `test_provision.py`. Commit.

### Task 2: the serial number counter and `flash.sh bind`

**Files:** `tools/provision.py` (`next-sn`), `tools/flash.sh` (`bind`), `tools/test_provision.py`, `.gitignore`.

- `provision.py next-sn COUNTER`: under an exclusive lock on `COUNTER.lock` (`fcntl.flock`, held for the whole read–increment–write), reads the integer in COUNTER (missing: 1), writes number + 1 to a temporary file in the same directory, `flush` + `os.fsync`, `os.replace` over COUNTER, `fsync` of the directory, and only then prints `SB` + YYMM (today, local time) + the number in 6 digits. Any failure before the print exits non-zero and prints no SN. A programming that fails after the print wastes a number, never reuses one. Past 999999: refuses (Codex plan r1).
- `flash.sh bind KEY [SN]`: reads the UID (as now), takes SN from the argument or `next-sn` (counter `$SBIOT_SN_COUNTER`, default `~/.sbiot/sn_counter`), builds the record, writes and verifies it as now, and prints the MAC and SN it wrote. `SBIOT_BIND_UID` (the bench's clone case) still works.

- [ ] Test (Python): two `next-sn` calls give consecutive numbers; 8 processes calling it at once get 8 different numbers; a write failure (a read-only directory, `os.replace` made to raise) prints no SN and leaves the counter as it was; a counter at 999999 refuses; the date part is YYMM.
- [ ] Fix as above. Run `test_provision.py`. Commit.

### Task 3: the bootloader and the app read the identity from the record

**Files:** `firmware/boot/boot.c` (the `bind_check` result `BIND_EOLD`: log "bind: an old record without MAC and SN: program it again with flash.sh bind", halt like `BIND_ENONE`, nothing erased), `app/app_main.c` (read the record once at boot through XIP: `bind_valid` then `bind_identity`; the MAC to `netmgr_init`, the SN to `sysmgr`), `components/sysmgr/sysmgr.c` (`sysmgr_serial` returns the record's SN, set by `sysmgr_set_serial`), `components/netmgr/netcfg.c` (`netcfg_mac` and `netcfg_serial` stay for the tests and the tool's vector only, or go if nothing uses them).

- [ ] The app halts as today when `bind_valid` fails (spec §11.5); with a v2 record it never derives a MAC or SN from the UID.
- [ ] Build boot and app; the host tests that use `netcfg_mac` (`test_netcfg`) still pass or are reduced to what remains.
- [ ] Commit.

### Task 4: the factory calibration tool

**Files:** `tools/factory_cal.py` (new), `app/bench_io.c` (only if the console's `A` needs a read-back line it does not print yet), spec §12.4.

- A console read-back (Codex plan r1): a new command `C` (free: `F` is the serial peer, `X` the XOFF injection; Codex plan r2) reads both factory record copies from SPI4 and prints, for each, whether it parses (magic, version, CRC) and its four coefficients.
- `factory_cal.py PORT G1 O1 G2 O2`: sends `A` with the four coefficients over UART7 (`board.py`'s serial helpers), then `C`: both copies must parse and hold the requested values; then a reset, and after the boot `/api/status` must report `aiCalibrated: true` (the runtime reads the record at boot only). The production path to SPI4 is the console because the team's SWD loader programs QSPI only.
- [ ] Test (Python, no board): the command line it builds and the parse of the `C` answer, on a recorded console transcript (two good copies, one bad copy, values that differ from the request).
- [ ] Board: write the nominal coefficients (gain 1, offset 0) and read them back; `/api/status` reports `aiCalibrated: true`.
- [ ] Commit.

### Task 5: the key audit gate

**Files:** `firmware/tools/audit_keys.py` (ported from `firmware/tools/audit_keys.py` of the ESP32 tree: the STM32 schema `stm32/firmware/components/cfgmodel/cfgmodel.c`, the sources `stm32/firmware/components/**/*.c` and `app/*.c` minus cfgmodel, flashdb and www), with `UNSUPPORTED` for keys accepted but not acted on, each with its reason.

- [ ] Run it; for each unread key: read it where the spec says it acts, or list it in `UNSUPPORTED` with the spec's reason (a key the UI shows but the STM32 build does not support). The ledger lists every one and the choice.
- [ ] Gate: `python3 stm32/firmware/tools/audit_keys.py` exits 0. Commit.

### Task 6: board, spec, review

**The one-time migration** of a board programmed before this plan (Codex plan r1 and r2): its staging may hold an older valid image, which any boot with a valid record installs over the new app, and its factory slot holds an older app that refuses a v2 record. No migration code in the bootloader; the order below never lets a boot install the old image:
1. While the old firmware runs: its console `Z` then `WIPE` erases all of SPI4 (staging, factory slot, FlashDB, the factory record) and resets. The old bootloader then seeds the factory slot from the old app: step 4 replaces it.
2. Flash boot and app of this plan over SWD. The new bootloader halts at the v1 record (before `install_run`): nothing is installed.
3. `flash.sh bind KEY`: the v2 record; the boot that follows finds staging empty and runs the new app (FlashDB formatted fresh: default configs).
4. The new console command `R` then `RESEED` erases the factory slot's first sector (its magic) and resets; that boot seeds the factory slot from the new app.
5. If the board was calibrated: `factory_cal.py` again (step 1 erased the factory record).

The boards programmed before this plan are development boards: losing their configs and calibration is accepted. The order goes into §12.4.

- [ ] Board:
  - the migration on the bench board, whose staging and factory slot both hold a valid older image: steps 1–4; at step 2 it halts with the old-record message, nothing erased or installed; after step 3 it runs the new app; after step 4 the log shows the factory slot seeded from it; `/api/status` shows the SN `SB<YYMM><n>` and the same MAC as before;
  - a Reload restore (held 5 s at power-up) boots the v2 factory image, which accepts the record;
  - the clone case of `ota_check` (`SBIOT_BIND_UID`) still erases and halts;
  - `defaults_check`, `web_check plain configs`, `review_check`.
- [ ] Spec: §11.5 (the v2 record, the old-record halt, MAC and SN), §12.4 (the SN counter, `factory_cal.py`), roadmap row 11 (11a done, 11b next).
- [ ] Codex code review, PR, merge.
