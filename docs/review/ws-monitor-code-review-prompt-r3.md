# Code review r3: branch `ws-monitor`

Your r2 P2 is addressed in 9c9f4a5 (`git show 9c9f4a5`):

- `web_mon_wait(queued, high, &full_since, now)` (`web_core.c`, tested in `test_web_core`):
  - returns true while a client's queue is at `WEB_SEND_HIGH` or more and has been full for less than `WEB_MON_STUCK_MS` (2 s);
  - `full_since` resets once it has room.
- `console.c drain()` stops dequeuing while any WebSocket client is behind in that sense (`clients_behind`, `full_since` kept in `c->data[8..16)`).
  - The rest of a long write stays in the MessageBuffer for the next poll, after Mongoose has sent some.
  - A client full for 2 s no longer holds the drain; it gets nothing, and its frames are counted as dropped.
- **On the board** (the user agreed to the test on their Modbus bus): 2000 bytes of 0xFF sent from a WebSocket client to UART2 arrived as two TX lines of 1460 and 540 bytes, 2000 in all. Edge's polls went on around them.

Findings as P1 / P2 / minor with file:line. End with **APPROVED** or **CHANGES NEEDED**.
