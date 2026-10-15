Đã sửa 4 điểm của vòng 1 trong `stm32/docs/plans/2026-10-15-07b-io.md` (mục "Codex round 1" ở cuối plan; `git diff 687bf3c..HEAD -- stm32/docs/plans/2026-10-15-07b-io.md`):

1. `adc_core_half(a, half, seq_start, seq_read, ctx)`: cộng xong mới gọi `seq_read`; test làm `seq` đổi trong lúc cộng.
2. và 3. `keepOutputs`: chỉ một bên ghi là task `reload` (poll 50 ms, prio 3). Dưới mutex nó lấy mask **mới nhất** (`io_core_persist_take`), ghi ngoài mutex, rồi `io_core_persist_done` (lỗi thì `dirty` lại). Task `io` và `iomgr_do_set` không bao giờ ghi flash, nên flash chậm không ảnh hưởng DI, DO, ADC. Ruling 3 viết lại theo hướng này; test có ca đổi mask trong lúc đang lưu.
4. `io_core_timers` tự áp hành động ngõ ra trong core và chỉ trả về yêu cầu restart, xử lý sau khi nhả mutex; không còn callback.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
