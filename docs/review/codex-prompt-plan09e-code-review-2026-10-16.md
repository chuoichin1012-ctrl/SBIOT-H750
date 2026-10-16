Plan 09d đã merge (PR #19). Người dùng yêu cầu **giảm giới hạn Edge về 200 điểm**, và không cần chạy lại các check trên board.

Commit 0aa1fc8 trên branch `plan09e-200-points`:
- `MAX_NODES` 200 ở `cfgmodel` (cùng câu báo lỗi "At most 200 points across all devices.") và ở UI `edge.ts`.
- Giữ nguyên phần của Plan 09d: body 96 000 byte, record 16 000 B, `edge_plan_batches`, báo lỗi kích thước ở UI.
- Test: `test_cfgmodel` 200/201; `test_edge_text` dùng model lớn nhất 200 điểm (64 104 B). Script bench (`mem_check`, `edge_check memory onedevice`, `web_check edge`) đổi sang 200 điểm nhưng không chạy, theo yêu cầu người dùng.
- Spec và plan đã cập nhật.

**Kết quả:** `test_cfgmodel`, `test_edge_text`, `npm run test:unit` (9/9) và `build:firmware` đều đạt; app build được.

**Phạm vi:** `git diff main HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi).
