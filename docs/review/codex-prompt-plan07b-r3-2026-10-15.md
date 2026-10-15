Đã sửa lỗi của vòng 2 (mục "Codex round 2" cuối plan `stm32/docs/plans/2026-10-15-07b-io.md`): nút Reload giờ được lấy mẫu trên tick 10 ms của task `io` (không bao giờ ghi flash); `RELOAD_FACTORY` notify task `reload`, task này chỉ làm việc chậm (factory reset, lưu `keepOutputs`). `test_reload` lấy mẫu mỗi 10 ms, có ca nhả ở 2,99 s (không reset).

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
