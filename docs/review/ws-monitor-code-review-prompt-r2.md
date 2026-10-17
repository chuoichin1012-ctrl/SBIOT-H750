# Code review r2: branch `ws-monitor`

Your r1 (`stm32/docs/review/ws-monitor-code-review.md`) is addressed in the last commit (`git show HEAD`):

- **P2:**
  - `console.c uart_frame` splits a frame with `web_mon_each(data, len, SERIAL_FRAME_MAX, record, &tx)`: one record per piece of ≤ 1460 bytes, all under one hold of `s_mb_lock`.
  - Each record that does not fit is counted in `s_dropped`.
  - 4000 typed bytes make 3 records, 4026 bytes with their lengths, which fit the 4 KB buffer.
  - `test_web_core`: 2000 bytes split as 1460 + 540; exactly 1460 is one record; an empty record still goes through.
- **Minor:**
  - `send_raw` calls the monitor before `uart_drv_write`, so an answer can never be queued ahead of its request.
  - When the write fails, it then calls the monitor with `(NULL, 0)`, which `web_mon_line` prints as `TX not sent` (tested).
  - On the board, Edge's polls show in order: `TX 04 03 …` then `RX 04 03 06 …`.

Findings as P1 / P2 / minor with file:line. End with **APPROVED** or **CHANGES NEEDED**.
