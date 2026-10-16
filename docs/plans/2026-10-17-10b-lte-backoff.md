# Plan 10b: LTE recovery backoff and soft power cycle

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** when the cell link stays without an address for a long time (no coverage, a blocked SIM), the module is no longer restarted every 2.5 minutes for ever. The waits grow, and a confirmed power cycle becomes the last resort (the user, 2026-10-17).

**Spec:** [2026-10-14-stm32-firmware-design.md](../specs/2026-10-14-stm32-firmware-design.md) §7.2 (recovery). **Builds on:** Plan 10 (merged, PR #21).

## Global Constraints

- **The recoveries that stay as they are:**
  - a silent module (no frame for 60 s and AT silent) is still RECOVER at once (CLD, RESET_N);
  - NO SIM still never resets the module while it answers;
  - PWRKEY is still never pressed for 3 s or more.
- **The backoff** applies to "no PPP address". The wait before acting grows with each attempt in a row that brought no address: **2, 5, 10, then 30 minutes**, and 30 minutes from then on. The first address resets it to 2 minutes.
- **The action:**
  - the first two attempts: `AT+CFUN=1,1`, as now, followed by RECOVER when AT stays silent;
  - from the third attempt on: a **soft power cycle**. `AT+QPOWD=1` (CLD first, on the plain UART), then `POWERED DOWN`, then PWRKEY 2.5 s from the confirmed OFF, as the start sequence does from OFF.
  - Without `POWERED DOWN` within 60 s, the module counts as UNKNOWN, and the RECOVER of today follows (RESET_N). It is never pressed blind.
- **Both paths share the one backoff** (Codex plan r1), over CMUX and on the plain UART:
  - on the plain path, the wait for the registration and the wait for an address both use the backoff's limit, not the fixed 90 s and 2 minutes;
  - a failure there takes the same step: RESET_N (the plain path's equivalent of CFUN, since data mode holds the UART) for the first two, the soft power cycle afterwards;
  - the counter is kept across CFUN, RECOVER and the power cycles, and is reset only by an address.
- **Keep it simple:** one pure counter in `lte_power.c`, host-tested; no new task, no config field.

## Rulings to approve

1. `lte_backoff_t {uint8_t fails;}`:
   - `lte_backoff_limit(b)` gives 2, 5, 10 or 30 minutes for `fails` 0, 1, 2 or ≥ 3;
   - `lte_backoff_step(b)` is called when the limit is reached without an address. It returns `LTE_ACT_CFUN` for the first two and `LTE_ACT_POWER_CYCLE` afterwards, and increments `fails` up to a cap;
   - `lte_backoff_ok(b)` sets `fails` to 0 on an address.
2. `lte_supervise` takes the limit as a parameter, in place of the fixed `LTE_NO_ADDR_MS`. Its RECOVER and NO SIM rules do not change.
3. `/api/status` gets `cell.recoveries`, the number of attempts in a row, for the bench and the UI.
4. **Bench:** `lte_check backoff`, with the bench toggle `q` dropping PPP's input. The bench build shortens the minutes to seconds through a console command `b` (`lte_bench_fast`), so that the case takes minutes, not an hour.
   - Checked: CFUN, CFUN, then a power cycle (QPOWD, POWERED DOWN, PWRKEY), at the growing intervals; then, with the input passing again, the address back and `recoveries` 0.
   - **The plain path** (the fallback forced with `j`): the same sequence of steps and waits.
   - **No POWERED DOWN** (Codex plan r1): the console command `d` makes `power_down()` disregard the URC, as if it never came.
     - The power cycle then goes through UNKNOWN, the RESET_N of a RECOVER, and the full start sequence: AT, RESET_N, then PWRKEY only after AT stays silent.
     - It never goes `lte_power_up(LTE_OFF)`, so PWRKEY is not pressed first.

## Tasks

### T1. The pure backoff (`lte_power.c`, `test_lte_power`)

- [ ] **RED:**
  - the limits 2, 5, 10, 30, 30 minutes;
  - the actions CFUN, CFUN, POWER_CYCLE, POWER_CYCLE;
  - an address resets both;
  - `lte_supervise` with a given limit;
  - RECOVER for a silent module before anything else, and NO SIM unchanged.
- [ ] **GREEN, run** `test_lte_power`, **commit.**

### T2. The board, bench, spec

- [ ] **`lte.c`:**
  - the limit from the backoff;
  - CFUN as now, or the power cycle (`power_down()`, then `lte_power_up(LTE_OFF)`);
  - `lte_backoff_ok` when PPP is up;
  - `cell.recoveries`;
  - the `b` console command (fast timings, bench only).
- [ ] **Bench:** `lte_check backoff` (over CMUX), `backoff_plain` (the fallback), `backoff_nopd` (no POWERED DOWN), then the regressions `mux`, `cfun`, `power` and `fallback`.
- [ ] **Spec §7.2,** measurements, **commit,** then the Codex code review, the PR and the merge.
