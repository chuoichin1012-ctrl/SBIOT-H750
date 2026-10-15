Plan 06a đã merge (PR #9). Xin duyệt **kế hoạch 06b trước khi code**: `stm32/docs/plans/2026-10-15-06b-web.md`, trên branch `plan06b-web`. Nội dung: Mongoose web, `/api/*`, UI, đồng hồ. Spec liên quan: §5.3, §6.1, §6.3, §6.4, §9.1, §9.3, §9.4, §10.

**Người dùng đã quyết:**
- mọi cấu hình, kể cả login, chỉ áp dụng sau khi khởi động lại; vì vậy bỏ F19 ở §6.4;
- giữ đơn giản;
- chỉ chạy các test bị ảnh hưởng.

**Xin kiểm đặc biệt:**
- **5 ruling cuối plan:**
  1. cert chỉ kiểm cấu trúc PEM cho tới Plan 08, khi có mbedTLS;
  2. `/api/io` trả 503 cho tới Plan 07;
  3. các chế độ UART của console WS chờ Plan 07;
  4. các dịch vụ trong status hiện "NOT STARTED";
  5. arena theo từng request.
- **Body theo luồng:** dùng idiom `c->pfn = NULL` tại `MG_EV_HTTP_HDRS` của Mongoose, kèm quy tắc 411/413/408.
- **Đồng hồ không dùng `mktime`/TZ:** offset trên `now_us`, timezone là số giờ nguyên.
- **Nhúng UI** bằng `.incbin` (`www_blobs.S`) thay vì `objcopy -I binary`.
- **Bench:** `web_check.py` chạy từ WSL, vì WSL tới được board qua NAT của Windows; Playwright lưu và tải lại từng trang.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách việc phải sửa.
