Plan 07a đã merge (PR #11). Đây là **Plan 07b**, nửa còn lại của roadmap row 07: `stm32/docs/plans/2026-10-15-07b-io.md` (branch `plan07b-io`).

Nội dung: `iomgr` (DI tích cực mức thấp, DO qua 74HC573, chế độ DO, 6 timer, `keepOutputs`), ADC3 theo §6.6 (oversampling ×16, TIM6 1 kHz, BDMA hai nửa, phục hồi), factory record (đọc; ghi bằng lệnh bench), nút Reload 3–15 s, `/api/io`. Logic thuần có host test: `io_core`, `adc_core` (kể cả factory record), `reload`.

Xin duyệt plan, đặc biệt 5 ruling:
1. Xung và nháy của DO chạy trên tick 10 ms của task `io`, không dùng software timer của FreeRTOS.
2. Factory record trong 07b chỉ ghi bằng lệnh bench `A`; loader sản xuất qua SWD vẫn ở Plan 11.
3. `keepOutputs` ghi trên task của bên gọi (như ESP32); timer đặt ngõ ra thì ghi trên `io`, có thể mất một cửa sổ ADC (đếm `late`).
4. IO Modbus slave (`iomgr_remote`) chuyển sang Plan 08, cùng Modbus gateway.
5. Bench chỉ có một nguồn 4–20 mA: hiệu chuẩn và độ chính xác với AI1, AI2 mắc nối tiếp; nhiễu 50/60 Hz, độ chính xác tuyệt đối và ảnh hưởng chéo với nguồn thứ hai để Plan 11 (P1); 07b đo ảnh hưởng chéo với AI2 hở mạch.

Đối chiếu spec §3, §5.2, §6.1, §6.3, §6.6, §9.1, §10.2, §11.4, §12.1, §12.2 và ESP32 `firmware/components/iomgr/iomgr.c`, `firmware/components/sysmgr/sysmgr.c` (Reload).

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa. Nhớ người dùng muốn giữ đơn giản.
