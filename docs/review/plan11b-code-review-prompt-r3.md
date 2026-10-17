# Code review r3: Plan 11b, branch `bench-hil`

Your r2 (`stm32/docs/review/plan11b-code-review-r2.md`) is addressed in the commit after 2a56b4b (`git show HEAD`):

- **P2:**
  - `mem_check.criteria` returns a failure for a `None` read, or for a read without `system.heapMin`, `system.heapLargest`, `mem.stackMin` (an object) or `mem.pools` (a list).
  - Scenario B no longer filters out failed reads.
  - `test_mem_check.Criteria`: a good read passes; a failed read and three incomplete reads cannot pass (they raised before the fix).
  - A live `/api/status` read gives `[]`.
- **Minor:** `test_net_cleanup` replaces `net_check.subprocess` with a `Popen` that raises, so the host test sends no LAN traffic.

Findings as P1 / P2 / minor with file:line. End with **APPROVED** or **CHANGES NEEDED**.
