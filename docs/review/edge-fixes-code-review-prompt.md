# Code review: branch `edge-fixes` (an outside review's three findings, and two UI requests from the user)

Please review `git diff main..edge-fixes -- stm32` (5 commits, 1746ba6..f51c595).

1. **Linkage** (`components/services/edge_core.c` `edge_link_step`, `test/host/test_edge_core.c`):
   - Every valid check now records `last_state` before minInterval is looked at; minInterval only holds back the action.
   - A threshold edge that rises during the wait is dropped, not deferred.
   - Follow/opposite are unchanged.
   - Tests: the outside review's scenario (ON ≥ 50, OFF ≤ 45, 1 s cycle, 5 s gap: false at 14 s, ON again at 15 s), plus "an edge in the wait is dropped".
2. **Reply loop:**
   - The UI refuses a request topic that matches the reply topic: `web/src/lib/rules.ts` `topicMatches`, `replyLoopError`, used by `Report.tsx`'s replyTopic check; unit tests.
   - `components/proto/rwprot.c` returns NULL (no answer) for a `dir: "up"` message; `test_proto.c`.
   - A warning for an Edge request topic that also matches a UART forwarding rule was left out (the user agreed).
3. **Reconnect report:**
   - `mqttgw_on_connect(fn)` is called from the mqtt task after a successful connect and `ms_resume`.
   - Edge registers `mqtt_up` when it reports over MQTT; it sets `s_resync` and notifies `edge_rpt`.
   - `edge_rpt` sends one whole report and clears the flag only when `report()` returns true (it now returns bool). An outbox that refuses it is tried again at the next wake (≤ 10 s).
   - Board case `edge_check resync`: on-change reports only, the relay cut and back, a report with the unchanged value; RED before, GREEN after.
4. **UI crash on Edit point** (the user):
   - A stored point without `ioPoint` crashed `nodeDefs` (`n.ioPoint.startsWith`).
   - `web/src/lib/edge.ts` `normalizeSlaves` reads the stored model with the firmware's defaults (`edge_dev_from_json` / `edge_pt_from_json`); `api.ts getEdge` applies it.
5. **Tables** (the user):
   - `DataTable` gets `selected` / `onSelect` (the first cell is a button).
   - The devices list is a table (Name, Port, Points; the detail is in the Port cell's title).
   - The linkage events are a table (On, Name, Point, Condition with its thresholds, Output with its action, Check / gap, Description); `linkageCondition` / `linkageOutput` with unit tests.
   - Seen on the board in Chromium; no page errors.

Constraints from the user: keep it simple; configs are applied at boot; the UI validates.

Findings as P1 / P2 / minor with file:line and a failure scenario. End with **APPROVED** or **CHANGES NEEDED**.
