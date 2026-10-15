Vòng 2 review code 06b: `git diff 57ab93a..HEAD -- stm32/`. Đã sửa 5 lỗi của vòng 1, xem mục "Code review round 1" cuối plan.

1. **Body ngắn hơn khai báo:** `on_message` so `hm->body.len` với `declared`. Thiếu thì không có tác dụng phụ.
2. **Pipelining:** xóa tới đầu body bằng `body_offset`, trong `upload_start` và `reject`.
3. **Hàng đợi gửi:**
   - `c->is_full` ngừng đọc khi `c->send` > 4 KB, ở cả web và console;
   - kết nối web còn 8 KB chờ gửi thì bị đóng ở request kế tiếp;
   - lệnh console không được trả lời khi `c->send` > 4 KB.
4. **Console:**
   - tối đa 5 socket trên listener;
   - socket chưa upgrade sau 5 s thì bị đóng;
   - web cũng đóng kết nối rảnh quá 60 s.
5. **GET Edge:** ghim trước khi gửi header; thất bại thì trả 503.

Mỗi điểm có một case trong `web_check.py` (`case_review`), đã chạy trên bản chưa sửa:
- 4/5 case RED;
- case thứ 3 được siết cho tới khi RED (mất 70 KB heap khi chưa sửa, 14 KB khi đã sửa).

Trên bản đã sửa:
- `web_check.py` đạt toàn bộ;
- `npm run test:pages` đạt.

Ngoài ra (yêu cầu của người dùng):
- UI dùng logo, mark và favicon SVG;
- CMake báo rõ khi UI chưa được build.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
