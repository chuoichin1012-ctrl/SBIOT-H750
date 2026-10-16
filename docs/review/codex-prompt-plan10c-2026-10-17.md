Plan 10c: `stm32/docs/plans/2026-10-17-10c-review-fixes.md`. Plan này sửa các lỗi mà bạn tìm ra khi review toàn bộ dự án (`stm32/docs/review/codex-gpt-6-astra-full-review-2026-10-17.md`): Critical 1, Important 2–10 và Minor 11–13. Người dùng đã đồng ý sửa tất cả trước Plan 11.

**Nguyên tắc:**
- mỗi lỗi một bản sửa nhỏ nhất, kèm test nếu có chỗ test trên host tới được;
- serial (task 4) và NO SIM trên LTE (task 8) là code chạy trên board, không có harness host. Bench cho hai phần này cũng không chạy được: board không có vòng nối RS485, và không thể tháo SIM lúc này. Hai điểm này được ghi lại bằng ruling.

**Chọn lựa chính:**
- **task 1:** `CJSON_NESTING_LIMIT=16` cho toàn bộ firmware và test host. UI giới hạn template cũng ở 16;
- **task 5:** `mb_map_t` có hai hook `begin`/`end` quanh vòng đọc FC3/FC4. Edge khóa cache một lần cho cả vòng đọc;
- **task 6:** quyền sở hữu staging OTA nằm trong `ota_app.c`, dùng chung cho web và UART7;
- **task 9:** hết thời gian chờ thì đóng kết nối khi hàng gửi không tiến triển;
- **task 11:** Status lấy cờ enabled từ việc dịch vụ đã khởi động lúc boot.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa trong plan. Người dùng muốn giữ đơn giản.
