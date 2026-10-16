Đã sửa điểm của vòng 3 theo cách đơn giản bạn đề xuất (mục "Codex round 3" cuối plan `stm32/docs/plans/2026-10-16-08a-bridge.md`):
- gộp lại thành một lớp `NET_AGAIN` (`EWOULDBLOCK`, `EAGAIN`, `ENOMEM`, `ENOBUFS`), vì lwIP tự đổi `ERR_MEM` thành `ERR_WOULDBLOCK` khi ghi non-blocking;
- quy tắc 10 s là chính sách "không tiến triển" với mọi nguyên nhân tạm thời, kể cả lwIP thiếu bộ nhớ: ngoại lệ có chủ đích của §5.6 cho TCP, và §5.6 sẽ ghi cùng câu khi plan được duyệt. Bỏ khẳng định "peer không đọc". `NET_AGAIN` tự nó không bao giờ đóng socket hay bỏ dữ liệu;
- UDP: `NET_AGAIN` giữ datagram, không giới hạn thời gian;
- `net_classify` có host test với giá trị errno của lwIP;
- một lần thử gửi thất bại cũng tính là có làm việc, nên lượt đó kết thúc bằng `vTaskDelay(1)`.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
