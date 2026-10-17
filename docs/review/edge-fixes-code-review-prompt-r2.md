# Code review r2: branch `edge-fixes`

Your r1 (`stm32/docs/review/edge-fixes-code-review.md`) is addressed in the last commit (`git show HEAD`):

- **P2-1:**
  - `edge_link_t.has_fired`: minInterval holds an action back only after a trigger. A condition that is true at the first check acts at once.
  - `test_edge_core`: "true at the first check: acts" (min 10 s, first check at 2 s); RED before the fix.
- **P2-2:**
  - `normalizeSlaves` starts each device and point from cfgmodel's `ITEM_SLAVE` / `ITEM_NODE` (copied) and applies `take()`'s coercions:
    - bool: a bool, a nonzero number, "1" or "true";
    - number: a number, a bool, or a numeric string;
    - text: text.
  - Tests: FC 3, timeout 200, range 1, port 502, poll 1000, uart 1, `onChange: 1`, `mapEn: "1"`, `reg: "7"`.
- **Minor:** `topicMatches` never matches a `$` topic with a leading `+` or `#` (as `mqttutil.c`); tests.

Also in the commit, at the user's request:
- The devices table shows Port (UART n, `IP:port`, IO, Calculated) and the Modbus address (the unit ID for TCP), with `→ mapAddr` when mapped.
- `Column.nowrap` keeps those cells on one line, and the two panels are now equal width.
- `edge_check onedevice` waits up to 45 s for the headroom line instead of a fixed sleep. It had relied on the slow `/api/log`.

On the board:
- `edge_check` (whole suite) passed but for onedevice's timing, which is now fixed.
- `change`, `resync` and `onedevice` were rerun and are ok.
- The UI was seen in Chromium with no page errors.

Findings as P1 / P2 / minor with file:line. End with **APPROVED** or **CHANGES NEEDED**.
