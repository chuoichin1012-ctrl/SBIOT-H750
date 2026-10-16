Plan 09b (luồng cấu hình) đã làm xong trên branch `plan09b-config`, từ 850bdc6eb73dbcdcd748a8ee5b3ae8bc2a99f045 tới 413e398. Đây là bước review code bắt buộc trước PR.

Plan: `stm32/docs/plans/2026-10-16-09b-config.md`. Xin đọc kỹ mục "Findings and measurements". Trong lúc làm, người dùng ba lần đổi hướng theo phía đơn giản hơn (ghi ở ruling 4 và trong ledger):
1. Thiết bị chạy với cấu hình được cung cấp.
2. Bộ nạp chỉ kiểm kiểu JSON, không tự ý đổi giá trị UI gửi.
3. UI kiểm từng field khi người dùng sửa và hiện lỗi ngay tại field.

Đó là quyết định của người dùng, không cần duyệt lại. Xin kiểm code theo đúng hướng đó.

**Các commit chính:**
- T1 (1f20581): default KV và factory reset.
- T2 (ada2033): đường lưu thuần, cộng khóa FAL sau một lần ghi lỗi.
- T3 (eb6fcfb, d872d7c, 414a566): bộ nạp chỉ kiểm kiểu JSON; baud 0 không còn gây chia cho 0.
- T4 (353d57b và commit sau): `rules.ts`, lỗi hiện ngay tại field.
- T5: bench, spec, roadmap.

**Kết quả kiểm thử:**
- Host test xanh. `npm run test:unit` đạt 7/7.
- Bench trên board: `web_check plain factory` và `test:pages` đạt.
- Hồi quy: `bridge_check tcpserver`, `edge_check basic remote`, `mqtt_check connect publish`, `tls_check tcpclient` đạt.

**Phạm vi:** `git diff 850bdc6eb73dbcdcd748a8ee5b3ae8bc2a99f045 HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi), và nhớ là người dùng muốn giữ đơn giản.
