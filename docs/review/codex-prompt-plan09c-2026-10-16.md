Xin duyệt Plan 09c: `stm32/docs/plans/2026-10-16-09c-ram.md` (branch `plan09c-ram`). Plan viết theo các câu trả lời D1–D7 của bạn (`codex-gpt-6-astra-plan09c-design-2026-10-16.md`), có đưa vào các chi tiết bạn bổ sung:
- `store_edge_abort` và chỉ một phiên ghi Edge tại một thời điểm;
- generation mới cao hơn mọi record đang có;
- test scanner khi cắt ở mọi vị trí, nạp từng byte, nạp theo chunk, và kết thúc giữa một token;
- giải phóng buffer ở mọi đường thoát và zeroize khóa riêng;
- đo block trống lớn nhất, chạy vòng lặp để kiểm ổn định, và tách chi phí theo điểm, theo thiết bị/task và theo buffer report.

Người dùng muốn giữ đơn giản. Trả lời APPROVED, hoặc NOT APPROVED kèm danh sách cần sửa.
