Plan 07a đã code xong và chạy trên board. Branch `plan07-serial-io`. Trước khi tạo PR, xin **review code**:
- `git diff main..HEAD -- stm32/`;
- plan `stm32/docs/plans/2026-10-15-07a-serial.md`: phát hiện S1–S5, đo đạc, 4 ruling đã duyệt (r3);
- spec §6.4, §6.5, §9.1.

**Trọng tâm:**
1. `components/serial/serial_frame.c` (`test_serial_frame` GREEN):
   - thời gian ký tự, RTO, t1.5/t3.5, BRR;
   - đọc ring theo lap và NDTR (overrun một và hai lap);
   - cắt transparent (`packetLength`, `packetInterval`) và RTU (vi phạm, resync sau t3.5 im lặng);
   - XON/XOFF, bộ điều khiển TX (pause, deadline, lỗi DMA).
2. `components/serial/uart_drv.c`:
   - ISR LL riêng (trong ITCM), receiver timeout;
   - DE phần cứng (`HAL_RS485Ex_Init`, 16 sample time);
   - TIM2 CC1/CC2 làm one-shot t3.5; RTO khi one-shot còn chạy thì tính là vi phạm;
   - DMA1 S2–S5, DMAMUX 41–44, TRBUFF; ring và buffer TX trong SRAM1;
   - sự kiện vào queue, `lost` khi queue đầy (không dùng `xQueueOverwriteFromISR`).
3. `components/serial/serial.c`: `serial_rx[n]` (ưu tiên 26), subscriber, mutex bus, `serial_transact` (unit, `SERIAL_DROPPED`), cửa sổ trả lời sau `serial_write`, heartbeat.
4. `components/webapi/console.c`: chế độ UART, một MessageBuffer một writer; `api.c`: bộ đếm `ports[n]` và `mem.uart`.
5. Bộ nhớ: các buffer lớn chuyển từ DTCM sang AXI (store, web, console, bench_ota), `check_elf.py` kiểm ITCM và SRAM1.
6. Bench: `app/bench_serial.c` (E/F) và `test/bench/serial_check.py` dùng hai cổng RS485 của board nối với nhau (S1).
7. **S5, board từng chạy firmware khác:** `tools/flash.sh erase` (mass erase flash nội và QSPI) và lệnh console `Z` + dòng `WIPE` (xóa toàn bộ SPI4, rồi reset). Xin cho ý kiến: để `Z` trong firmware chính (cạnh `u` và `w` trên console UART7) có chấp nhận được không, hay chỉ cho bản bench?

**Trên board (sau khi xóa sạch cả hai chip):**
- `serial_check.py` 25/25 (vi phạm t1.5 do board tự đếm; 60 s ở 230 400 kèm flood LAN: 1 357 824 byte, 0 overrun, 0 lost; XOFF tiêm vào giữa DMA: dừng 1059 ms, đủ 2048 byte);
- `web_check.py` 42/42, `store_check.py` đạt;
- `check_elf.py` đạt.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa (lỗi thật, theo mức độ). Nhớ người dùng muốn giữ đơn giản.
