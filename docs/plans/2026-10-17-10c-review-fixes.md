# Plan 10c: whole-project review fixes

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (Native, chosen) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix every finding of Codex's whole-project review (`stm32/docs/review/codex-gpt-6-astra-full-review-2026-10-17.md`), each with the smallest change and a test, before Plan 11.

**Architecture:** No new subsystem. One limit for all JSON parsing (cJSON's nesting limit), small guards where a value from a config or the network indexes or waits, one owner for the OTA staging slot, a stall timeout on web sends, and four UI/status corrections. Pure logic goes where a host test reaches it; board code that no host test reaches is named as such.

**Tech Stack:** C11 on STM32H750 (FreeRTOS, lwIP, Mongoose, cJSON, FlashDB); Preact/TypeScript web UI; host tests in `stm32/firmware/test/host` (`make`), web unit tests (`npm run test:unit`).

**Spec:** `stm32/docs/specs/2026-10-14-stm32-firmware-design.md`

## Global Constraints

- Keep it simple (the user): the smallest fix, no new machinery, no new error states.
- Configs are read once at boot; a save needs a restart to apply.
- The UI validates; the gateway checks auth, name, size, JSON syntax and write errors; the loader keeps memory safe only, it does not judge business rules.
- Storage: survive power loss and transient faults; persistent hardware faults go read-only.
- Run only the affected tests after an edit.

## Review Focus

1. A JSON document of exactly the new depth limit (16) still parses everywhere a real config or template needs it: edgeDevices (depth 4), a nested report template.
2. A second firmware upload while one is staged (web or UART7) is refused, and a failed or cut upload frees the slot for the next.
3. A web client that stops reading its answer loses its slot within the stall time; a slow but moving download (the log, the Edge model) is not cut.
4. A serial transaction whose bus never goes quiet returns timeout and frees the bus.
5. NO SIM on the plain UART never resets the module; a SIM inserted later is seen within 60 s.

---

### Task 1: JSON nesting limit (Critical 1)

**Files:** `firmware/CMakeLists.txt`, `firmware/test/host/Makefile` (`CFLAGS`), `web/src/lib/edge.ts` (`templateError`); tests `test/host/test_proto.c` (rwprot), `test/host/test_edge_text.c` or the test that compiles `edgecore.c` (`edge_tmpl_compile`), `web/tests/unit/rules.test.ts`.

- [ ] Test (Codex plan r1: a request that is otherwise valid, so only the depth decides): `{"rw_prot":{"ver":"1.0","dir":"down","id":"7","r_data":[{"name":"x"}]},"z":` + D nested arrays + `}`. At the limit (the whole document 16 levels deep) `rwprot_handle` answers; one level more, and 300 levels, it returns NULL (cJSON refuses the document), with no crash. `edge_tmpl_compile` the same at 16 and 17 levels. Run: FAIL (17 and 300 levels answer: the limit is 1000).
- [ ] Fix: `-DCJSON_NESTING_LIMIT=16` for the firmware (CMake `add_compile_definitions`) and the host tests (`CFLAGS`). cJSON then refuses deeper input at `parse_value` and deeper output at print; 16 levels cost about 0.8 KB of stack (48 B a level, Codex's measure).
- [ ] UI: `templateError` refuses a template deeper than 16 (`The template nests more than 16 levels.`); a web unit test pins it.
- [ ] Run the tests that compile cJSON (`test_proto`, `test_edge_text`, `test_edge_core`, `test_cfgmodel`, `test_store`) and web unit. Commit.

### Task 2: the config cache keeps a name it does not own (Important 2)

**Files:** `firmware/components/store/store.c` (`cache_put`, `store_set_text`); test `test/host/test_store.c` (small-cache build).

- [ ] Test (`#if STORE_CACHE_SIZE < 5000`): store a config too large to fit beside the defaults (raw_put), reboot (it is skipped: "no memory for its text"), shrink it with `store_set_text` called with the name in a stack buffer, overwrite that buffer, then `store_get_json("<name>")` returns the saved text. Run: FAIL (the entry's name is the overwritten buffer).
- [ ] Fix: `cache_put` stores the canonical name: the pointer `cfg_names()` gives for that name (static, cfgmodel's table). A helper `static const char *canonical(const char *name)`.
- [ ] Run `test_store`, `test_store_small`. Commit.

### Task 3: an index from a config (Important 3)

**Files:** `firmware/components/services/bridge_core.c/.h` (new `const char *bridge_mode_name(int work)`), `bridge.c:932`; test `test/host/test_bridge_core.c`.

- [ ] Test: `bridge_mode_name(-1)`, `(5)`, `(INT_MIN)` are `"unknown"`; `0..4` the five names. Run: FAIL (no such function).
- [ ] Fix: `bridge_mode_name` bounds the index; `bridge.c` logs through it. Check every other array indexed by a config number in `bridge.c`, `bridge_core.c`, `mqttgw.c`, `iomgr`, `edge_rt.c` (grep `[c.`/`[b->C.`/`[cfg`) and bound any that can go negative or past the end the same way; list them in the ledger.
- [ ] Run `test_bridge_core`; build. Commit.

### Task 4: a serial transaction that never ends (Important 4)

**Files:** `firmware/components/serial/serial.c:261`.

- [ ] Fix: the wait for a frame still arriving shares the transaction's deadline (`until`): past it, give the bus back and return 0, as the tail wait above it does.
- [ ] Ruling to record: `serial.c` is board code with no host harness; the fix mirrors the loop above it. A bench case needs the RS485 loop this board lacks (Plan 10 ruling): not run.
- [ ] Build. Commit.

### Task 5: one snapshot per Modbus register read (Important 5)

**Files:** `firmware/components/proto/include/modbus.h` (`mb_map_t`: optional `bool (*begin)(void *ctx)` and `void (*end)(void *ctx)`), `proto/modbus.c` (`mb_serve`, the FC3/FC4 loop), `services/edge_rt.c` (`map_read_reg` without its own lock; `begin` = `cache_lock`, `end` = `cache_unlock`); test `test/host/test_proto.c`.

- [ ] Test: a fake map counts `begin`/`end` and records whether each `read_reg` ran between them:
  - an FC3 read of 4 registers: `begin` once, the 4 reads inside, `end` once;
  - `begin` returns false: the answer is exception `MB_EX_DEVICE_FAILURE`, no read, no `end`;
  - `read_reg` fails on the 3rd register: the exception answer, `end` exactly once;
  - a map without hooks reads as before.

  Run: FAIL (no hooks).
- [ ] Fix: `mb_serve` calls `begin` before the FC3/FC4 loop (false: `MB_EX_DEVICE_FAILURE`, nothing read, no `end`) and `end` once after it, on every return after a successful `begin`. In `edge_rt.c`, `begin` takes the cache lock, `map_read_reg` reads without locking, `end` unlocks. Writes keep their own path: no lock held while writing to the bus.
- [ ] Run `test_proto`, `test_edge_core`; build. Commit.

### Task 6: one owner for the OTA staging slot (Important 6)

**Files:** `firmware/components/ota/ota_app.c/.h` (the owner, `OTA_EBUSY` if absent, `void ota_stage_release(ota_stage_t *st)`, `bool done` in `ota_stage_t`), `components/webapi/web.c` (`upload_end`), `app/bench_ota.c`; test `test/host/test_ota.c` (or the test that links `ota_app.c`).

**The contract** (Codex plan r1):
- One owner, `static ota_stage_t *s_owner`, taken and given back with an atomic compare-and-swap (`__atomic_compare_exchange_n`: LDREX/STREX on the M7, the same on the host); web and UART7 run in different tasks.
- `ota_stage_begin(st, …)` takes it (NULL → st) **before** it touches `st` or erases staging; refused: `OTA_EBUSY`, nothing changed. If `begin` then fails (an erase error), it gives the owner back.
- `ota_stage_finish(st)`: OK sets `st->done` and keeps the owner until the reboot; a failure gives it back.
- `ota_stage_release(st)` gives it back only if `st` holds it and is not `done`; any other caller (one that was refused) changes nothing. Cleanup after a success (the connection closing) keeps the image.

- [ ] Test:
  - A begins, B begins → `OTA_EBUSY`; B's cleanup (`release(B)`) leaves A's session going, and A's writes and finish succeed;
  - after A's finish OK: B begins → `OTA_EBUSY`; `release(A)` (A's cleanup) → still held; B begins → still `OTA_EBUSY`;
  - a failed finish, or `release` by the owner before finishing → B begins OK;
  - a `begin` that fails after taking the owner (the simulated erase error) gives it back.

  Run: FAIL.
- [ ] Fix as the contract says. `web.c` answers 409 on `OTA_EBUSY` ("A firmware image is waiting for the restart, or another upload is in progress.") and calls `ota_stage_release` from `upload_end` (it does nothing after a success); `bench_ota.c` (UART7) does the same on its failures.
- [ ] Run the OTA host tests; build. Commit.

### Task 7: `rw_prot` out of memory (Important 7)

**Files:** `firmware/components/proto/rwprot.c:59-86`; test `test/host/test_proto.c` with cJSON allocation failure hooks.

- [ ] Test: a sweep failing cJSON allocation n = 0, 1, … for a request with 3 reads and 2 writes: `rwprot_handle` returns NULL or a valid JSON answer, never leaks (ASan/LSan). Run: FAIL (leak).
- [ ] Fix: no list → no items built (stop); an item whose add fails is deleted; a NULL `header()` answers NULL.
- [ ] Run `test_proto`. Commit.

### Task 8: NO SIM on the plain UART (Important 8)

**Files:** `firmware/components/netmgr/lte.c` (the caller of `plain_ppp`).

- [ ] Fix: `PLAIN_NO_SIM` keeps the module running: no `recover()`, wait `LTE_RETRY_MS` (60 s, as over CMUX) and start the loop again from ON (AT answers, CMUX is asked again, refused, the SIM asked again). Only a silent module (AT failing) recovers.
- [ ] Ruling to record: board code; the bench cannot take the SIM out now (the user's board): not run on the board.
- [ ] Build. Commit.

### Task 9: a web client that stops reading (Important 9)

**Files:** `firmware/components/webapi/web_core.c/.h` (new `typedef struct { uint32_t since; bool on; } web_send_t;` and `bool web_send_poll(web_send_t *t, size_t queued, bool wrote, uint32_t now_ms)`, the stall time `web_stalled` uses), `web.c` (the slot holds a `web_send_t`; `MG_EV_WRITE` and `MG_EV_POLL` call it; a stall closes the connection, whose close path already frees an Edge pin); test `test/host/test_web_core.c`.

**When the clock runs** (Codex plan r1): it starts when the send queue goes from empty to not empty, restarts at each write that sent bytes (`wrote`), and stops when the queue is empty. `web_send_poll` returns true when it has run for the stall time.

- [ ] Test:
  - a connection idle for an hour, then an answer queued: not stalled at once, nor before the stall time;
  - queued, nothing written for the stall time: stalled;
  - queued, a write every second for twice the stall time: never stalled;
  - the queue empties, then fills again later: counted from the new fill;
  - wrap-safe across `UINT32_MAX`.

  Run: FAIL (no such function).
- [ ] Fix as above. A moving download refreshes its clock at every write: not cut.
- [ ] Run `test_web_core`; build. Commit.

### Task 10: a placeholder that is only partly a literal (Important 10)

**Files:** `firmware/components/proto/edgecore.c:396`; test where `edge_tmpl_compile` is tested.

- [ ] Test: placeholders `1abc`, `null-device`, `truex` render as JSON strings; `-1`, `null`, `true`, `2.5` stay literals; the whole report parses as JSON. Run: FAIL (`1abc` copied raw).
- [ ] Fix: `cJSON_ParseWithOpts(ph, &end, true)`: a literal only when the whole text parses.
- [ ] Run the test. Commit.

### Task 11: minor UI and status fixes (Minor 11–13)

**Files:** `web/src/lib/rules.ts` (`systemPortClash`), `web/src/pages/edge/Report.tsx`, `firmware/components/webapi/api.c` (`enabled()`), the MQTT and Edge services (a getter for "started at boot"); tests `web/tests/unit/rules.test.ts`, `test/host/test_web_core.c` if the status fields pass through it.

- [ ] 11: `systemPortClash` also reports `websocketPort` equal to `webPort` while the console is on (`websocketTarget` > 0); a unit test.
- [ ] 12: `/api/status` `mqtt.enabled` and `edge.enabled` come from whether the service started at boot (`mqttgw_started()`, `edge_started()`, set by their start functions), not from the saved config.
- [ ] 13: Report's "choose periodic or scheduled" error only when no point reports on change (`onChange`); a unit test if the check moves to a helper.
- [ ] Run web unit, tsc, affected host tests; build. Commit.

### Task 12: board, spec, review

- [ ] Flash; `defaults_check`, `pages.mjs`, `web_check plain configs`, `net_check`; a `rw_prot` request 300 deep over the bridge or MQTT does not crash the board (`/api/status` uptime keeps growing); a second OTA upload while one is staged answers 409; `edge_check` cases that need no RS485 loop if any.
- [ ] Spec: the cJSON nesting limit (§9), the OTA staging owner (§11), the web send stall (§10), NO SIM on the plain UART (§7.2).
- [ ] Codex code review, PR, merge.
