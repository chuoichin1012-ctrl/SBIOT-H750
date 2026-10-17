# Code review: Plan 11b (bench HIL runs and gates), branch `bench-hil`

Please review the code of branch `bench-hil` against `main`: the commits from d672917 (Task 1) to a0e2599 (Task 5). The plan is `stm32/docs/plans/2026-10-17-11b-bench-hil.md`, which you approved in r4. The spec is `stm32/docs/specs/2026-10-14-stm32-firmware-design.md` §12.2 and §12.3. Read the diff with `git diff main..bench-hil -- stm32` and the files themselves. The run's report is `stm32/docs/reports/2026-10-17-11b-bench.md`.

## Firmware changes (the most weight)

1. **`/api/log` is resumable** (cc7a521).
   - `components/store/store.c`, `include/store.h`: `store_log_iter(uint32_t from, fn, ctx)` and `store_log_meta_t.seq`.
   - `components/webapi/web.c`: the `ST_LOG` pump keeps `log_next`, the next flash sequence number, instead of skipping `log_sent` lines from the oldest one on every pass. Before, that walk was O(n²): a 288 KB log took 27–60 s, now 0.7 s.
   - `app/app_main.c`: console `l` passes 0.
   - `test/host/test_store.c`: `test_log` covers sequence numbers growing, a start in the middle and a start after the newest.
   - Please check:
     - phase 0's end condition (`x.index == 0`);
     - lines appended or rolled over while the page streams;
     - the first pass with `from` 0;
     - `fdb_time_t` versus `uint32_t`;
     - the RAM phase, which still skips by count.
2. **The idle task's stack** (513ec31): `configMINIMAL_STACK_SIZE` 256 → 320 (spec §12.3: IDLE kept only 916 B free). DTCM is 120 256 / 122 880 (`check_elf`).
3. **Console `S` prints `erases %lu`** (5291d50, `store_progress()`): the evidence of FlashDB GC that scenario B uses.

## Bench scripts

`stm32/firmware/test/bench/`:
- `bench_env.py`: `resolve_gw`, `gates`, `judge`, `qualify`.
- Gates wrapped around the stresses in `serial_check`, `bridge_check`, `mqtt_check`, `disc_check`, `store_check`, and `ota_check` (the UART7 transfer).
- `mem_check.py`:
  - `criteria`, `case_a`, `case_b`, `handshakes`, `overlapping`;
  - host tests in `test_mem_check.py`.
- `win_peer.py`: `manyclients`, `slowget`, `tlsset`. `tlsset` holds each accepted connection until every port has one, at most 6 s (the gateway's handshake timeout is 10 s).
- Migrations: `edge_check` (the 26 KB template is stored as sent; at start-up reports turn off), `io_check` (the flip timing waits for DI1 to drop), and `boot_check` / `store_check` / `net_check` / `disc_check` as ledgered.

Please check:
- **Bench evidence:** that no bench case can pass without the evidence its message claims.
- **Restores:** that every case restores what it changes. Constraints: `network.priority` stays 0; any `network` write is the whole saved document; nothing re-attaches the LTE modem.
- **The report:** that it claims nothing the suites do not show.

## Constraints from the user (keep)

- Keep it simple.
- Configs are read once at boot.
- The UI validates; the device runs with what it is given.

## Your answer

Findings as P1 / P2 / minor, each with file:line and a concrete failure scenario. End with **APPROVED** or **CHANGES NEEDED**.
