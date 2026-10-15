Plan 07b (đã duyệt r3) đã code xong và chạy trên board, trừ phần đo ADC thật (board hiện tại chưa hàn VREF; sẽ đo trên board mới trước PR). Branch `plan07b-io`. Xin **review code**: `git diff main..HEAD -- stm32/`, plan `stm32/docs/plans/2026-10-15-07b-io.md` (mục Bench findings I1–I5, Measurements, và ledger rulings bên dưới).

**Trọng tâm:**
1. `components/iomgr/io_core.c` (`test_io_core`): debounce DI (mẫu đầu tính một tick như ESP32), chế độ DO, timer (giữ mốc cao nhất khi đồng hồ lùi ≤ 120 s — lệch có chủ đích so với code ESP32), `persist_take/done` (một bên ghi, luôn mask mới nhất).
2. `adc_core.c` (`test_adc_core`): bàn giao nửa buffer (`seq_read` gọi sau khi cộng), lỗi, chuyển µA, factory record (giới hạn hệ số, NaN).
3. `adc_drv.c`: HAL_ADC_Init theo §6.6, TIM6 TRGO = update, BDMA/DMAMUX2 bằng thanh ghi, ISR trong ITCM, OVRIE tắt trong ISR đến khi phục hồi, trình tự phục hồi §6.6, `_Static_assert` thời gian scan.
4. `iomgr.c`: task `io` (không ghi flash; DI, nút Reload lấy mẫu 10 ms; timer; latch 74HC573), task `reload` (factory reset, lưu `keepOutputs`), mutex, factory record đọc từ SPI4.
5. `sysmgr/reload.c` (`test_reload`): 3–15 s, giữ từ đầu bị bỏ qua tới khi nhả, nhả phải thấy 2 mẫu.
6. `api.c` `/api/io` GET/POST và `/api/status` (`aiCalibrated`, `mem.adc`); `app/bench_io.c` (`a`, `A`, `J`, `D`).
7. UI `stm32/web/src/pages/Io.tsx` (theo yêu cầu người dùng: công tắc relay, đèn DI, AI µA, làm mới 1 s).

**Ledger rulings** (`.superpowers/sdd/2026-10-15-07b-io/progress.md` không nằm trong repo; tóm tắt): không dùng cờ `dirty` (so với mask đã lưu); `frec_choose` nhận hai bản sao thô; `reload` cần 2 mẫu nhả; không sinh lại `app.ioc` (driver tự cấu hình ADC3/TIM6/BDMA, `adc.c` chỉ để có `hadc3` và MspInit); `iomgr_start` nhận `nor_t` của SPI4; bỏ `lroundf` (không có libm); bench chỉ có một cặp DO1→DI2; đo xung/nháy bằng trace trên board vì HTTP qua NAT bị treo/lệch; ca "giữ Reload lúc app khởi động" không kiểm trên bench (người dùng cho qua, host test che); `serial_check` chờ nối lại dây RS485 (người dùng đã tháo).

**Trên board:** host test `test_io_core`, `test_adc_core`, `test_reload` GREEN; `io_check adcbase` (thanh ghi, 10,01 ngắt/s, lỗi giả công bố lại sau 148 ms) và `dido` đạt; Reload 1 s / 20 s không làm gì, 5,75 s và ~12 s khôi phục; `web_check` 43/43, `store_check` đạt, `check_elf` đạt.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa (lỗi thật, theo mức độ). Nhớ người dùng muốn giữ đơn giản.
