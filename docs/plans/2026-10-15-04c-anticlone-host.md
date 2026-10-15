# Plan 04c: anti-clone binding to the chip UID, host first

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Firmware copied onto another STM32H750 does not run. The check of spec §11.5 exists as plain C over `nor_t`, and the production tool writes the record. Both are proven on the host against each other. The board step (`flash.sh bind`) has been run once on the bench board.

**Architecture:**
- **`tools/provision.py`** (production PC): `genkey`, `pubkey`, `record` and `uid`. It signs `"SBIOT-H750 bind"` ‖ UID with Ed25519, using Python `cryptography`. The private key never leaves that PC.
- **`tools/flash.sh bind KEY|dev`:**
  1. reads the UID over SWD;
  2. makes the record;
  3. writes it at QSPI 0x903F0000 through the team's loader, with verify.
- **`components/ota/bind.c`:** `bind_valid()` for the app, and `bind_check()` for the bootloader.
  - Missing, unreadable or corrupt record: halt, nothing written.
  - A valid record whose signature fails for this chip: erase the application slot, keep the record, halt.
  - The bootloader never writes a record.
- **`third_party/monocypher`:** Monocypher 4.0.2, `crypto_ed25519_check` only.
- **CMake:** `SBIOT_BIND_PUBKEY_FILE` (32 bytes) generates `gen/bind_key.h`. Without it, the build uses the dev key and warns.
- **Board side:** Plan 04b calls `bind_check()` first in the bootloader and `bind_valid()` in the app.

**Tech Stack:** C11, gcc with ASan/UBSan, GNU make, Python 3 (`cryptography` 41), Monocypher 4.0.2, STM32CubeProgrammer CLI.

**Spec:** `stm32/docs/specs/2026-10-14-stm32-firmware-design.md` §5.1, §11.5, §12.1 (anti-clone row), §12.4 (production steps). **Roadmap:** row 04c.

## History

- **First version** (commit f9ce1cc, never published): an HMAC-SHA256 tag that the bootloader wrote itself at the first boot. This was the user's choice (b) at the time.
- **The user then moved the tag to programming time** (2026-10-15): "chuyển sang ghi tag lúc nạp code, bootloader chỉ kiểm, không bao giờ tự tạo tag". This version replaces the first one.

## Rulings (for review)

1. **Ed25519 instead of HMAC.** The user did not answer the algorithm question, so I took my recommendation.
   - With HMAC, the verifying key is the signing key. The app check would put it in QSPI, readable by anyone who removes the chip, and anyone could then make records.
   - With Ed25519, the device holds only the public key.
   - Cost if wrong: about 10.9 KB more in the bootloader (measured at `-Os` with `--gc-sections`; the bootloader is 18 KB today and its limit is 64 KB), plus a vendored library.
2. **A missing record halts without erasing.** A genuine board that was not provisioned yet must not lose its image. Cost if wrong: none for security, because a clone without a valid record does not run either.
3. **A corrupt record (wrong CRC) halts without erasing.** This is a rule for sorting errors, not a proof. A wrong CRC is treated as damage, because random damage almost always breaks it. A forger has to recompute the CRC, and that path erases. (Codex corrected this wording.) Cost if wrong: an attacker who leaves the CRC broken keeps the application in QSPI, but it still does not run.
4. **The signature is checked over this chip's UID.** The UID stored in the record is only informative. A record of another chip fails even when its stored UID is rewritten with the CRC fixed (`test_clone`).
5. **Development keypair** derived from the public string `"SBIOT-H750 dev bind key"`. Builds without `SBIOT_BIND_PUBKEY_FILE` embed its public key, warn at configure time, and the bootloader prints `bind: DEV KEY` (Plan 04b). Cost if wrong: a production build made without the variable accepts dev-bound boards. The warning, the banner and the production steps in §12.4 are the guard.

## Review Focus

1. **A record copied from a genuine board onto another chip, or forged** (UID changed, CRC fixed, another key). Expected: erase the slot, keep the record, halt at every boot. Pinned by `test_clone`.
2. **A genuine board must never be erased** when its record is missing, cut short, corrupt or unreadable. Pinned by `test_not_provisioned` and `test_corrupt_and_errors`: every case asserts zero erases.
3. **Python signer against the C check:** the sample comes from `test_provision.py` and is checked with Monocypher in `test_bind`.
4. **A power cut while erasing a clone:** the next boot erases on. Pinned by `test_power_cuts`.
5. **Tests that pass without testing:** `mutants_ota.py` has 8 binding mutants. The two that survived at first exposed a redundant UID compare, which was removed.

## Files

| Path | Responsibility |
|---|---|
| `stm32/firmware/components/ota/{include/bind.h,bind.c}` | `bind_valid`, `bind_check` |
| `stm32/firmware/components/ota/include/flashmap.h` | `QSPI_BIND` 0x3F0000 |
| `stm32/firmware/components/ota/bind_key.h.in`, `stm32/firmware/CMakeLists.txt` | Public key at build time |
| `stm32/firmware/third_party/monocypher/*` | Monocypher 4.0.2 and its provenance (`README.md`) |
| `stm32/firmware/tools/{provision.py,test_provision.py}` | Production signer and its test, which writes the C sample |
| `stm32/firmware/tools/flash.sh` | `bind KEY\|dev` mode |
| `stm32/firmware/test/host/{test_bind.c,Makefile,mutants_ota.py}` | C tests and mutants |
| `stm32/firmware/test/bench/test_board.py` | `flash.sh bind` without a key never calls CubeProgrammer |

## Tasks

### Task 1: the production signer

- [x] `python3 stm32/firmware/tools/test_provision.py OUT`. Expected: `ModuleNotFoundError: provision` first, then `test_provision: ok`. The 18 checks cover:
  - the record layout, the signature, determinism and the dev key;
  - the UID parsed from CubeProgrammer output;
  - `genkey` never overwrites, and uses mode 0600;
  - `pubkey` and `record` never write over the key file, also through a symlink or hard link, and refuse a bad key before touching the output. This was a Codex finding: the first version opened the output before it read the key.

### Task 2: the check

- [x] `make -C stm32/firmware/test/host build/test_bind`. Expected: build fails against the earlier HMAC `bind.c` first. Then `build/test_bind build/bind_sample.bin` gives `test_bind: ok`, with 368 erases for a clone.
- [x] `python3 stm32/firmware/test/host/mutants_ota.py` → `mutants: 24 of 24 caught`.

### Task 3: build and board

- [x] Configure with a 32-byte public key: `BIND_DEV_KEY 0` and the bytes appear in `gen/bind_key.h`. With 31 bytes: configure fails. Without a key: a warning and `BIND_DEV_KEY 1`.
- [x] `python3 stm32/firmware/test/bench/test_board.py` → `flash.sh bind` without a key gives the usage message and never calls the CLI.
- [x] On the bench board, `stm32/firmware/tools/flash.sh bind dev` gave `flash: bind.bin (84 bytes) at 0x903F0000 verified`, `bound to UID 1f0032001251333039353330`. The CubeProgrammer line was `0x1FF1E800 : 0032001F 30335112 30333539`.
- [x] `make -C stm32/firmware/test/host` → every suite ok; boot and app build.
