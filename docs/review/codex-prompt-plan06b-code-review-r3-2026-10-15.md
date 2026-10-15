Vòng 3 review code 06b: `git diff a1bb4d5..HEAD -- stm32/`. Đã sửa hai điểm của vòng 2:

1. **`declared`** giờ được đặt ngay sau `hdrs_done`, trước mọi nhánh thoát sớm.
   - Case mới: POST rồi GET thiếu login trên cùng một kết nối.
   - Bản chưa sửa: chỉ có 1 câu trả lời, không có 401.
   - Bản đã sửa: 200 rồi 401.
2. **Test socket console:**
   - chỉ EOF hoặc reset mới tính là đã đóng; timeout làm case thất bại;
   - client mới kết nối trong lúc test vẫn giữ các socket;
   - kết quả: board tự đóng 8/8, client mới nhận 101.

`web_check.py` đạt toàn bộ trên board.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
