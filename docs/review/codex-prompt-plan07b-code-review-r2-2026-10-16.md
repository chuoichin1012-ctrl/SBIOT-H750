Đã sửa 2 lỗi Important của review code 07b vòng 1 (mục "Code review round 1" cuối plan `stm32/docs/plans/2026-10-15-07b-io.md`). Xin **review lại**: `git diff bd0070d..HEAD -- stm32/firmware`.

1. `io_index(double, n)` trong `io_core` (host test: 0, 1 hợp lệ; −0,5, 1,9, 0,5, −1, 2, 1e300, NaN, vô cùng bị từ chối); `POST /api/io` dùng nó, không ép kiểu trước khi kiểm. `web_check`: `-0.5` và `1.9` trả 400, relay không đổi.
2. `parse_time` đọc hết chuỗi: `HH:MM` hoặc `HH:MM:SS`, mỗi trường 1–2 chữ số, không ký tự thừa; `test_io_core` có 14 giờ sai và 4 giờ đúng.

Thêm: `serial_check` 35/35 sau khi nối lại dây RS485; `web_check` 44/44; host test GREEN.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
