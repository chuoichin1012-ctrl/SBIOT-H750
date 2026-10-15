Vòng 2 cho kế hoạch 06b. Đã sửa các điểm của vòng 1, xem mục "Codex round 1" cuối plan.

1. **Arena theo task:**
   - `arena_begin` ghi `arena_port_self()`: trên board là handle của task, trên host là fake;
   - `arena_current()` chỉ trả arena cho đúng task đó;
   - scope không bao giờ kéo dài qua hai callback;
   - test mới T2b.
2. **Client đọc chậm:**
   - mọi câu trả lời theo luồng chỉ thêm phần kế tiếp khi `c->send.len` < 4 KB: file UI (`app.js` 42 KB), GET Edge, log;
   - mọi đường thoát đều bỏ pin;
   - WS: client nào vượt 4 KB thì frame bị bỏ và đếm;
   - bench có hai client không đọc, kiểm heap ổn định.
3. **Giới hạn body** kiểm tại `MG_EV_HTTP_HDRS`; có test 13 KB với `Content-Length` và với chunked.
4. **SNTP:** khởi tạo dưới core lock, tên server tĩnh, offset 64 bit đọc ghi trong critical section.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách việc phải sửa.
