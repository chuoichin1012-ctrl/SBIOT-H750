- **P2 — `stm32/firmware/components/webapi/console.c:60–61`: successful writes are silently truncated.** Sending 2000 text bytes from the WebSocket page transmits all 2000 through `serial_write`, but the monitor displays only 1460. The missing 540 bytes are neither shown nor counted as dropped. Split larger writes into bounded monitor records, with continuation indicated, and count any rejected chunks. Add a test above `SERIAL_FRAME_MAX`.

- **Minor — `stm32/firmware/components/serial/serial.c:246–250`: RX can appear before its corresponding TX.** TX is recorded only when the sending task resumes after transmission completes. If that task is delayed, the higher-priority `serial_rx` task can enqueue the device’s reply first. The message-buffer mutex prevents corruption but does not preserve wire order. Capture TX ordering before transmission starts, with failed transmissions distinguished, or timestamp events at the driver boundary.

The 1460-byte text formatting fits, and the WebSocket send queue remains bounded. Focused console-line checks passed; no board tests were run.

**CHANGES NEEDED**