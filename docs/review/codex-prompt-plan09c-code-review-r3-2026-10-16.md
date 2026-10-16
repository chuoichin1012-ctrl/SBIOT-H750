Review code Plan 09c, lượt 3: sửa điểm Important của lượt 2 (`codex-gpt-6-astra-plan09c-code-review-r2-2026-10-16.md`), commit 3cbcbbf.

**Sửa:**
- `web_check.py`:
  - `req_retry()` tự connect (timeout 5 s). Chỉ khi chính `connect()` timeout thì nó chờ `RETRY_PAUSE` (2 s) và connect lại một lần. Sau đó request đi trên socket đã connect đó, qua `req(..., sock=sock)`.
  - Một timeout khi gửi request hay khi chờ phản hồi thì ném lỗi như `req`, không thử lại.
  - `req` đóng kết nối trong `finally`.
  - `heap_loop` dùng `req_retry` và đếm số lần thử lại.
- Test mới `test/bench/test_retry.py`, chạy với server local, không cần board:
  - connect timeout lần đầu (giả lập `socket.create_connection`) thì được thử lại, request trả 200, và có đúng 2 lần connect;
  - server nhận request rồi không bao giờ trả lời thì ném timeout, không thử lại.
  - Trước khi sửa, test này đỏ (chưa có `req_retry`); sau khi sửa thì đạt.

**Board:** `web_check heap_loop` đạt 200 vòng, mọi endpoint trả đúng mã, 2 connect phải thử lại, heap 356 168 → 356 168 B.

**Phạm vi:** `git diff a99f48f HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi). Nhớ là người dùng muốn giữ đơn giản.
