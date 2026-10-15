Plan 06b đã merge (PR #10). Bước tiếp theo là Plan 07, tôi tách thành:
- **07a:** serial;
- **07b:** iomgr, Reload, ADC3, factory record.

Xin duyệt **kế hoạch 07a trước khi code**: `stm32/docs/plans/2026-10-15-07a-serial.md`, trên branch `plan07-serial-io`. Spec liên quan: §5.3, §5.4, §6.1, §6.4, §6.5, §9.1, §10.1. Baseline hành vi là `firmware/components/serial/serial.c` của ESP32.

**Bench:** hai USB-RS485 (UART1/UART2) trên COM Windows, nguồn dòng 4–20 mA, dây DI/DO, nút Reload. Không có RS232.

**Xin kiểm:**
- 4 ruling;
- tách logic thuần `serial_frame` (vòng DMA, cắt frame transparent/RTU, đếm overrun bằng số vòng HT/TC, XON/XOFF, timing/BRR);
- dùng ISR LL riêng thay HAL;
- TX qua buffer 2 KB;
- cách đo t1.5 ở 1200 baud trên bench.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách việc phải sửa. Người dùng muốn giữ đơn giản.
