# Plan 05a: storage logic on FlashDB, host first

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `components/store` runs on FlashDB over any `nor_t`. It keeps:
- every small config as compact text in a 20 KB cache;
- `edgeDevices` in 8 KB records by generation, with commit, cleanup and GET pinning (spec §9.4);
- certificates, runtime state and the persistent log.

It is proven on the host against the NOR simulator with power cuts.

**Architecture:**
- **`third_party/flashdb`:** FlashDB 2.2.0, as the ESP32 firmware uses it, plus one patch (B1).
- **`components/store`:**
  - `fal_nor.c`: a FAL device over a `nor_t`, limited to FlashDB's 1 MB at offset 0;
  - `port/{fdb_cfg,fal_cfg}.h`: the FlashDB configuration and the four partitions of spec §5.2;
  - `store.c`: ported from `firmware/components/store/store.c`. The platform supplies `store_port.h`: a recursive lock, the clocks, and a log line.
- **The text cache:** one 20 KB region. Saving a config moves the others to close the gap, then appends the new text.
- **`edgeDevices`:**
  - `store_edge_save` runs `cfg_edge_text` (strict) and writes its output into 8 KB records `ed.<g>.<i>` through a static 8 KB buffer;
  - the manifest `@<g>,<count>` is the commit point;
  - afterwards every record that is neither the new generation nor pinned is deleted;
  - a GET pins a generation (`store_edge_pin` / `store_edge_record` / `store_edge_unpin`), and a superseded pinned generation is deleted at its last unpin.
- **Board side:** Plan 05b provides the FAL flash (SPI4 with DMA and a bounce buffer), the FreeRTOS port, `syslog` and the heap wrapper.

**Tech Stack:** C11, gcc with ASan/UBSan, GNU make, FlashDB 2.2.0, cJSON 1.7.19, the Plan 03 text pipeline (`cfg_validate_text`, `cfg_edge_text`).

**Spec:** `stm32/docs/specs/2026-10-14-stm32-firmware-design.md` §5.3 (static budget), §5.5 (`store` text cache), §9.1 (FlashDB, the patch), §9.4 (records, commit, cleanup, pinning), §12.1 ("Store Edge" row). **Roadmap:** row 05, split into 05a (host) and 05b (board).

## How this plan was made

As with Plans 02 and 04b, the code was written and run before this document. The tests were written first, against the new `store.h`; mutants then proved them. The tasks record what was done.

## Findings

- **B1. FlashDB 2.2.0 loses the old value of a KV whose rewrite was cut.**
  - How it was found: the power-cut matrix on a save that collects garbage. A cut while the manifest's name or value was being programmed (operations 83 and 84 of 89) left no manifest after the reboot, so the store showed the default model.
  - Cause: `_fdb_kv_load` → `check_and_recovery_kv_cb` marks the half-written KV (`PRE_WRITE`) as `ERR_HDR` and returns `true`, which stops the scan. The old value (`PRE_DELETE`) lies further on and is never recovered in that load, and lookups only see `WRITE` KVs. Upstream `master` has the same code.
  - Patch (`third_party/patches/flashdb-2.2.0-sbiot.patch`): scan again until no half-written KV is left. With it, all 89 cut points and 150 runs with three cuts end on the old or the new model.
- **B2. The store made B1 worse.** Its boot cleanup deleted every `ed.*` record not of the manifest's generation. With the manifest missing, that meant all of them, so even the next boot, where FlashDB would have recovered the manifest, could not get the model back. Now a boot without a manifest deletes nothing, and the next successful save removes whatever it does not keep (`test_no_manifest_keeps_records`, RED then GREEN).
- **B3. A power cut leaks memory in the simulation.** The `longjmp` leaves the store holding heap blocks; a real cut clears the RAM. The cut matrices therefore run in a second pass (`STORE_CUTS=1`) without leak detection. The cases run first, with it.

## Codex round 1 (NOT APPROVED), and the fixes

Every fix began with a test that failed first.

1. **A commit FlashDB reports as failed can still be in place.** A write failing once at each of the 89 operations of an edge save (`test_write_failures`) found two cases.
   - At operation 86, the new manifest is written but deleting the old one fails, so the model is lost after the reboot.
   - At operation 85, the new manifest's final status write fails. A read-back then saw the new manifest through FlashDB's cache, so the store deleted the old model.

   Fix: after a failed manifest write, the store mounts the edge database again, as the next boot will, and takes the committed generation from there. The model served is then always the one the next boot finds.
2. **An unreadable or lost manifest let the next POST delete the model** before validating it. Fixes:
   - the new generation is numbered above every record in flash, so a save never deletes anything before its commit;
   - the manifest is read into a fixed buffer, so no allocation can fail there.

   Test: `test_lost_manifest`.
3. **Endless loops holding the lock**, with deletes or status writes that silently change nothing (`test_no_endless_loops`, with an `alarm` guard).
   - The store's cleanup now runs at most 8 passes of 16.
   - FlashDB's rescan (the patch) runs only after a status write that worked, and at most 16 times.
4. **`port2` certificates** (spec §10.2) are accepted and kept across a reboot.
5. **Spec §5.3 budget:** the AXI static total now includes the 8 KB record buffer.

While doing 1, a test aimed at FlashDB alone (`test_gc_write_failures`) found two more FlashDB faults. A live KV written once, while another KV is rewritten 40 times through garbage collection, was lost to one failing write at 7 of 1347 operations:
- **B4. `move_kv` ignored a failed copy and a failed final status write** and deleted the source, and `do_gc` then formatted the sector.
- **B5. `move_kv` ignored a failed status write of its target sector,** so the KV landed in a sector still marked empty, which the next load skips.

Both are now in the one patch file, `flashdb-2.2.0-sbiot.patch`. That matrix and every other pass of `test_store` now pass. Mutants: 7 more, for each new fix.

## Codex round 2 (NOT APPROVED), and the fixes

Again each fix began with a test that failed first.

1. **Read failures during garbage collection lost a KV.** A new matrix (`test_gc_write_failures`, second half) fails reads of the sector holding a kept KV: once at any read, the mount's included (2 of 3618 lost it), and three times in a row after the mount (8 of 3032). Fixes:
   - **The FAL retries a read three times.** A read has no side effect, and FlashDB treats most read results as data, so a sector header that did not read back could even be formatted at mount.
   - **`move_kv` copies only what it read.**
   - **Any failed read flags the garbage collector,** which keeps a sector it was collecting when a read failed. A KV that did not read back looked like garbage, and an early end of the scan skipped KVs.
2. **An unfinished recovery let the mount succeed.** It now fails the load (`FDB_WRITE_ERR`) when:
   - a status write failed;
   - a `PRE_DELETE` move failed for another reason than space;
   - the 16 rescans ran out.

   The store then marks edge unavailable: no model, not even the default, and saves refused (500) until a reboot. Test: `test_no_endless_loops`, which asserts data and results, not only that it returns.
3. **An unknown commit could still delete the new generation** (old generation 0). After any failed write in a save, the store remounts and decides:
   - the new manifest is in place: committed;
   - the old one, or none when there was none: the new records go;
   - anything else (mount failed, manifest unreadable, another generation): edge unavailable, nothing deleted, the next boot decides.

   Test: the write-failure matrix also from an empty edge database.

## Codex round 3 (NOT APPROVED), and the fixes

1. **`read_kv` acted on a header it did not read** (it could write `ERR_HDR` over a good KV), and the mount could format a sector whose header did not read back. Ruling 8 is therefore replaced: patching each read site of FlashDB would never end, so the guard moves under FlashDB.
   - **A read that fails past the FAL's three tries makes the flash read-only:** every write and erase is refused until `fal_nor_rearm()`, which only a new boot (`store_init`) calls. FlashDB can then misread, but it can never write or erase on the strength of a misread.
   - **The store notices** (`fal_nor_read_failed()`) and turns read-only until the next boot: saves are refused with 500, edge is unavailable, and certificates and edge records just read are not handed out.
   - Tests: the FlashDB read matrix gains a third pass, three failures in a row anywhere including the mount, with half the buffer garbage, as a transfer cut short would leave it. It was RED (1 of 3618 lost the KV), then GREEN. There are also two store matrices: reads failing three in a row at every 7th read of a boot (`test_boot_read_failures`) and at every 5th read of an edge save, the remount included (`test_save_read_failures`). Nothing is lost, and nothing half saved is served.
2. **A failed factory reset left RAM and flash apart.** Each database is now reset on its own, and the config cache is reloaded from the flash as it now is.
   - A failed edge reset is mounted again, since edge is the one database whose state the store keeps in RAM (its generation). `test_factory_reset_fails` covers cfg and edge each failing partway, and was RED, then GREEN.
   - A remount of cfg and of cert proved unnecessary (their mutants survived): FlashDB stays consistent there, and certificates are always read from flash. Both were removed.
3. **The "commit not known" mutant** is now equivalent by construction, and the reason is recorded in `mutants_ota.py`. That branch needs a failed remount or a manifest that does not read back, so a read or a write failed. After a read failure the FAL refuses every write, so the branch's deletes cannot happen. New mutants pin the latch on writes and on erases, and the edge remount after a failed reset.
4. **Two FlashDB read hunks of round 2 were dropped,** because their mutants survived once the latch was in place: `move_kv` copying only what it read, and `do_gc` keeping a sector after a failed read. The latch covers both, and every other read site of FlashDB too. The patch file is back to the four write and recovery faults.
5. **The store mutants run faster:** about 8.5 minutes instead of 15 to 20.
   - `STORE_FAILFAST` stops `test_store` at the first wrong case, and the runner skips the cut pass once the quick pass has failed.
   - FlashDB is built from source only for a FlashDB mutant.
   - Most mutants are now caught in 3 to 5 s.

## Codex round 4 (NOT APPROVED), and the fixes

1. **`store_state_get` handed out a value it had misread.** It now returns 0 once a read has failed for good (`test_state_read_failures`, RED then GREEN).
2. **A read failure before the edge mount left edge enabled,** so a store that was read-only still served `[]`. Edge now serves and saves only through `edge_usable()`: its generation known, and nothing misread. The boot read matrix now asserts that a read-only store serves no model, also on a flash with no model (RED, then GREEN).

Store mutants: 26 of 26.

## Fault scope (agreed with the user, 2026-10-15)

- **Survived:** a power cut at any moment, and transient read faults.
- **Persistent hardware faults** (a flash that keeps failing writes or reads): the storage must only not make things worse. The flash turns read-only, saves are refused and an error is reported; further operation is not required.
- A fault class outside this scope is recorded under known limitations, not fixed further.

**Known limitations (accepted by the user, not fixed):**
- **Codex round 5** found that a persistent write or erase failure, with reads still working, does not latch the storage read-only. Later saves still try the flash and report the failure. The user's decision: "Thay vì sửa flashdb, chúng ta chấp nhận lỗi đó, đừng làm dự án phức tạp thêm" (accept it rather than patch FlashDB further).
- No recovery from a broken flash part.
- Power loss on the board, and the SPI4 driver, are proven in Plan 05b, not here.

## Rulings (for review)

1. **Edge sector size 16 KB**, against 64 KB on the ESP32. An 8 KB record and its header fit, and 16 sectors in 256 KB keep the garbage collection fine-grained. Cost if wrong: more sector headers (a few hundred bytes).
2. **A static 8 KB record buffer** in the store, instead of a block from the arena. Plan 03 measured the Edge pipeline close to the arena's size. §5.3 is updated. Cost if wrong: 8 KB of AXI.
3. **At most 4 pinned generations.** A further GET gets `count = 0`, which the web layer answers with 503 (§9.4). Cost if wrong: a 503 under an unusual load of slow GETs during repeated saves.
4. **The text cache is 20 KB, as in spec §5.3.** The defaults take 3.7 KB. A save that does not fit is 413, with cache and flash unchanged (`test_cache_full`, built with a 3800-byte cache). Cost if wrong: very long publish/subscribe/linkage lists hit 413.
5. **The scratch buffer of `store_set_text`** (`CFG_SMALL_BUF`) comes from `cJSON_malloc`, so it uses the caller's arena scope, or the heap otherwise. The same holds for `store_get`'s tree. `store_get_json` returns a heap copy, keeping the ESP32 contract (`api.c` frees it).
6. **With no model saved,** a GET pins generation 0, which is one record holding the default text `[]`.
7. **FlashDB's own messages** go to `printf` (`FDB_PRINT`). On the board, Plan 05b routes them to the log.
8. **A read that fails past three tries makes the whole storage read-only until the next boot,** not just the database it hit. That is the simplest rule that keeps every FlashDB write behind a good read. Cost if wrong: one bad read stops all saves, the DO state included, until a restart. The board logs it.

## Review Focus

1. **A power cut at any point of an edge save,** including during garbage collection and during the manifest rewrite. Afterwards the model must be exactly the old one or exactly the new one, with no stray records, and the store must keep working. Pinned by the matrix (89 cut points, garbage collection asserted), 150 runs with three cuts, and a save after each.
2. **GET against POST:**
   - a pinned generation stays whole through new saves and through 30 saves that keep collecting garbage;
   - it is deleted at the last unpin;
   - pins do not survive a reboot.

   Pinned by `test_pinning` and `test_pin_through_gc`.
3. **A rejected save changes nothing:** invalid, not JSON, too many points, or a cache that is full. The committed text, the generation and the flash all stay the same. Pinned by `test_edge_save_and_read`, `test_set_get` and `test_cache_full`.
4. **The cache's moves:** after a save, every other config's text is intact (`test_set_get`).
5. **Re-validation at boot:** a copy from older firmware gets the missing keys, and an unusable copy falls back to the default (`test_revalidate_at_boot`).

## Files

| Path | Responsibility |
|---|---|
| `stm32/firmware/third_party/flashdb/*`, `third_party/patches/flashdb-2.2.0-sbiot.patch` | FlashDB 2.2.0, the patch, provenance |
| `stm32/firmware/components/store/{include/store.h,include/store_port.h,include/fal_nor.h}` | API, port, FAL device |
| `stm32/firmware/components/store/{store.c,fal_nor.c,port/fdb_cfg.h,port/fal_cfg.h}` | The store, FAL over `nor_t`, configuration |
| `stm32/firmware/test/host/test_store.c`, `test/host/Makefile` | Cases, power-cut matrices; `test_store_small` |
| `stm32/firmware/test/host/mutants_ota.py` | 11 store mutants, among them the FlashDB patch |

## Tasks

### Task 1: FlashDB

- [x] Vendored from the 2.2.0 tag (SHA-256 recorded in `third_party/flashdb/README.md`). `src`, `inc` and `port/fal` are identical to the ESP32 copy.

### Task 2: store and its tests

- [x] `store.h` written, then `test_store.c` against it, then `store.c` and `fal_nor.c`.
- [x] `STORE_QUICK=1 build/test_store`: the cases pass. The one first failure was in the test itself: a 128-point model is invalid, not too large.
- [x] The cut pass with garbage collection failed at operations 83 and 84 (B1, B2). Fixed: the FlashDB patch, and the boot cleanup no longer runs without a manifest. Then `test_store: ok` in both passes and in `test_store_small`.

### Task 3: mutants and the whole suite

- [x] `python3 stm32/firmware/test/host/mutants_ota.py` → `mutants: 37 of 37 caught` (26 earlier, plus 11 store mutants, among them the FlashDB patch).
- [x] `make -C stm32/firmware/test/host` → every suite ok.
