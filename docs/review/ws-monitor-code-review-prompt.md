# Code review: branch `ws-monitor` (three requests from the user)

Please review `git diff main..ws-monitor -- stm32`.

1. **The WebSocket console in UART mode shows every frame of the port, both ways** (the user: Edge polls on UART2 were invisible; only subscriber RX was shown, and a transaction's answer never reaches subscribers).
   - `components/serial/serial.c`, `include/serial.h`: `serial_monitor(port, fn, ctx)`, one per port.
     - It is called in `send_raw` after a successful `uart_drv_write`, so every writer (`serial_write`, `serial_transact`), from its own task, with the bus held.
     - It is also called at the top of `deliver()` (serial_rx[n]), before the transaction or subscriber split.
   - `components/webapi/console.c`:
     - records hold a direction byte plus the frame, in the MessageBuffer (now 4 KB, in `.axi_bss`);
     - two writer tasks are serialised by `s_mb_lock`, a static mutex taken with a 5 ms wait, else dropped and counted;
     - `drain()` sends one text frame per record, `"TX 01 03 ...\n"` / `"RX ...\n"`, via `web_mon_line`;
     - LOG mode is unchanged.
   - `components/webapi/web_core.c`: `web_mon_line`, host-tested in `test_web_core` (format, the largest frame, a short buffer).
   - `web/src/lib/console.ts` (`appendLines`, `lineKind`, unit tests) and `pages/Websocket.tsx`: coloured lines (TX brand, RX green) in a log div, at most 2000 lines.
   - On the board, with Edge polling a real device on UART2: `TX 04 04 00 00 00 01 31 9F` / `RX 04 84 01 92 C1` pairs.
   - `test/bench/serial_check.py`: `ws_recv_bin` also reads `RX` lines. It was not run: a real device is on UART2 now.
2. **Device rows** (`components/ui.tsx` `DataTable`): a click anywhere on a row selects it. The action cell stops propagation, and the first cell's button stays for the keyboard. Seen on the board in Chromium: add a device, click another row's Port cell, and its points show.
3. **Save asks first** (`components/feedback.tsx` `askSave`, `lib/save.ts`):
   - a restart-type save opens "Save settings?" and sends nothing until "Save and continue editing" (POST, toast, `after`) or "Save and restart" (POST, then `restartDevice`);
   - Cancel and Esc send nothing.
   - On the board: 0 POSTs after Save, Cancel or Esc; 1 POST after "Save and continue editing".

Check in particular:
- the monitor callback's blocking and ordering (TX from several tasks, RX from serial_rx[n]);
- the MessageBuffer's single-reader and multi-writer use;
- frames over `SERIAL_FRAME_MAX`;
- the text size for a 1460-byte frame;
- the WebSocket send buffer.

Findings as P1 / P2 / minor with file:line and a failure scenario. End with **APPROVED** or **CHANGES NEEDED**.
