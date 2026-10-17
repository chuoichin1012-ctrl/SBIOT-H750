Plan 11a, review code vòng 4. Có một thay đổi mới sau khi bạn APPROVED ở vòng 3 (commit 10e949c, `app/bench_ota.c` `rx()`).

Bộ giám sát mới bắt được một lỗi thật trên board. Trong ca `ota_check` "upload bị cắt sau 8 KB", `HAL_UART_Receive` thăm dò UART trong suốt 30 s chờ khối dữ liệu, ở ưu tiên của `init` (7). Task `io` (ưu tiên 6) bị đói CPU hơn 10 s, và board bị reset với log `wdog: io stopped for 10 s`.

**Sửa:** `rx()` nhận theo từng lượt 20 ms; khi một lượt hết thời gian thì nhường CPU 1 ms bằng `vTaskDelay(1)`, rồi nhận tiếp tới hết thời hạn. Số byte đã nhận được tính lại từ `RxXferCount`. FIFO 16 byte của UART7 (đã bật trong `usart.c`) giữ dữ liệu tới trong lúc nhường; nếu vẫn mất byte thì SHA-256 của ảnh sẽ bắt được.

**Board:** `ota_check` đạt toàn bộ, kể cả các upload đầy đủ 580 KB và ca bị cắt 8 KB. Không có thêm lần reset nào do bộ giám sát (boot 181 tới 197). `web_check plain configs` và `review_check` đạt.

Phạm vi: `git diff 10e949c~1 10e949c -- stm32`. Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important.
