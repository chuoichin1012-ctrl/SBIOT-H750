# Plan 11a: MAC and serial number at programming, the factory calibration tool, the key audit

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (Native, chosen) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** LED WORK tells normal running, a firmware transfer or install, and a halt apart; the programming tool writes each board's MAC and serial number into its signed binding record; the firmware reads them from there and refuses to run without them; a production tool writes the ADC calibration; `audit_keys.py` reports 0 unread config keys.

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

**Files:** `firmware/components/board/include/board.h` and `board_io.c` (`board_iwdg_reload()`, `board_wdi_toggle()`; `board_kick()` keeps doing both for the SWD loader and early boot), new `components/board/pace.c/.h` (pure: `#define PACE_RUN_MS 500u`, `PACE_BUSY_MS 100u`, `PACE_HALT_MS 250u`; `bool pace_due(uint32_t now_ms, uint32_t *last_ms, uint32_t half_ms)`), `components/board/wdog.c` (loop every 100 ms: IWDG1 reloaded each loop, WDI toggled when `pace_due` with the mode's half period; `void wdog_set_busy(bool)`, `void wdog_set_halt(void)`), `boot/boot.c` (a paced `boot_kick`: IWDG1 every call, WDI by `pace_due` on `HAL_GetTick`; 100 ms while it checks, installs or restores, 500 ms while it samples Reload, 250 ms in `halt()` and the no-image halt), `components/webapi/web.c` and `app/bench_ota.c` (busy from the upload's claim to its end), `app/app_main.c` (the bind halt calls `wdog_set_halt`); test `test/host/test_board.c` (or a new `test_pace.c`).

- [ ] Test: `pace_due` is false before the half period and true at it, updates `last` only when due, survives the `uint32_t` wrap; a sequence of calls every 10 ms gives exactly one toggle per half period for each of 500, 100 and 250.
- [ ] Fix as above. `wdiMaxGapMs` still measures the wdog loop's gap (nominal 100 ms), so the 400 ms criteria of §6.1 and §12.2 stand.
- [ ] Build boot and app; board: the LED blinks at 1 s when running, 0.2 s during a web OTA upload (and the install after it), 0.5 s halted (an unbound board); `wdiMaxGapMs` stays under 400 ms.
- [ ] Spec: §6.1 (`wdog` row), §11.2 and §11.5 (the LED patterns). Commit.

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
| 36 | Ed25519 signature (64 B) over `"SBIOT-H750 bind2"` ‖ chip UID ‖ MAC ‖ SN |
| 100 | CRC-32 of bytes 0–99 |

- [ ] Test (C, host): a Python-signed v2 record verifies; a changed MAC, SN or UID with the CRC fixed is `BIND_CLONE`; a v1 record (`SBBD`) is the new `BIND_EOLD` and erases nothing; `bind_identity(rec, mac, sn)` returns the record's MAC and SN; an SN that is not `SB` + 10 digits fails the parse. Run: FAIL.
- [ ] Test (Python): `provision.py record KEY UID SN OUT` writes the layout above; the MAC equals `netcfg_mac`'s (a fixed UID vector shared with the C test); a bad SN is refused.
- [ ] Fix: `BIND_LEN` 104, `BIND_EOLD`, `bind_valid` over the v2 message, `bool bind_identity(const uint8_t rec[BIND_LEN], uint8_t mac[6], char sn[13])`. `provision.py`: `record KEY UID SN OUT` (the MAC computed inside), `next-sn COUNTER` (task 2).
- [ ] Run `test_bind`, `test_provision.py`. Commit.

### Task 2: the serial number counter and `flash.sh bind`

**Files:** `tools/provision.py` (`next-sn`), `tools/flash.sh` (`bind`), `tools/test_provision.py`, `.gitignore`.

- `provision.py next-sn COUNTER`: reads the integer in COUNTER (missing: 1), prints `SB` + YYMM (today, local time) + the number in 6 digits, and writes number + 1 back **before** printing (write to a temporary file, then rename), so a programming that fails after it wastes a number but never reuses one. Past 999999: refuses.
- `flash.sh bind KEY [SN]`: reads the UID (as now), takes SN from the argument or `next-sn` (counter `$SBIOT_SN_COUNTER`, default `~/.sbiot/sn_counter`), builds the record, writes and verifies it as now, and prints the MAC and SN it wrote. `SBIOT_BIND_UID` (the bench's clone case) still works.

- [ ] Test (Python): two `next-sn` calls give consecutive numbers; the counter is written before the SN is printed (a crash after: the next call skips it); a counter at 999999 refuses; the date part is YYMM.
- [ ] Fix as above. Run `test_provision.py`. Commit.

### Task 3: the bootloader and the app read the identity from the record

**Files:** `firmware/boot/boot.c` (the `bind_check` result `BIND_EOLD`: log "bind: an old record without MAC and SN: program it again with flash.sh bind", halt like `BIND_ENONE`, nothing erased), `app/app_main.c` (read the record once at boot through XIP: `bind_valid` then `bind_identity`; the MAC to `netmgr_init`, the SN to `sysmgr`), `components/sysmgr/sysmgr.c` (`sysmgr_serial` returns the record's SN, set by `sysmgr_set_serial`), `components/netmgr/netcfg.c` (`netcfg_mac` and `netcfg_serial` stay for the tests and the tool's vector only, or go if nothing uses them).

- [ ] The app halts as today when `bind_valid` fails (spec §11.5); with a v2 record it never derives a MAC or SN from the UID.
- [ ] Build boot and app; the host tests that use `netcfg_mac` (`test_netcfg`) still pass or are reduced to what remains.
- [ ] Commit.

### Task 4: the factory calibration tool

**Files:** `tools/factory_cal.py` (new), `app/bench_io.c` (only if the console's `A` needs a read-back line it does not print yet), spec §12.4.

- `factory_cal.py PORT G1 O1 G2 O2`: sends the console command `A` with the four coefficients over UART7 (`board.py`'s serial helpers), then `a`, and checks that the record read back holds them and `calibrated` is true. The production path to SPI4 is the console because the team's SWD loader programs QSPI only.
- [ ] Test (Python, no board): the command line it builds and the parse of the `a` answer, on a recorded console transcript.
- [ ] Board: write the nominal coefficients (gain 1, offset 0) and read them back; `/api/status` reports `aiCalibrated: true`.
- [ ] Commit.

### Task 5: the key audit gate

**Files:** `firmware/tools/audit_keys.py` (ported from `firmware/tools/audit_keys.py` of the ESP32 tree: the STM32 schema `stm32/firmware/components/cfgmodel/cfgmodel.c`, the sources `stm32/firmware/components/**/*.c` and `app/*.c` minus cfgmodel, flashdb and www), with `UNSUPPORTED` for keys accepted but not acted on, each with its reason.

- [ ] Run it; for each unread key: read it where the spec says it acts, or list it in `UNSUPPORTED` with the spec's reason (a key the UI shows but the STM32 build does not support). The ledger lists every one and the choice.
- [ ] Gate: `python3 stm32/firmware/tools/audit_keys.py` exits 0. Commit.

### Task 6: board, spec, review

- [ ] Board:
  - flash boot and app built from this branch: the bench board (its v1 record) halts with the old-record message, nothing erased;
  - `flash.sh bind dev`: it boots; `/api/status` shows the SN `SB<YYMM><n>` and the same MAC as before;
  - the clone case of `ota_check` (`SBIOT_BIND_UID`) still erases and halts;
  - `defaults_check`, `web_check plain configs`, `review_check`.
- [ ] Spec: §11.5 (the v2 record, the old-record halt, MAC and SN), §12.4 (the SN counter, `factory_cal.py`), roadmap row 11 (11a done, 11b next).
- [ ] Codex code review, PR, merge.
