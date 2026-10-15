Đã sửa 2 điểm của vòng 2 (mục "Codex round 2" cuối plan `stm32/docs/plans/2026-10-16-08a-bridge.md`):

1. Tách `NET_NOMEM` (`ENOMEM`, `ENOBUFS`) khỏi `NET_FULL` (`EWOULDBLOCK`, `EAGAIN`).
   - Thiếu bộ nhớ không bao giờ đóng socket hay bỏ datagram, dù kéo dài bao lâu, và không tính vào thời gian treo. Host test có ca 60 s `NET_NOMEM`.
   - Chỉ 10 s cửa sổ đầy mà không nhận thêm byte nào mới làm mất peer. Plan ghi đây là ngoại lệ có chủ đích của §5.6, và §5.6 sẽ thêm cùng câu đó khi plan được duyệt.
   - UDP: `NET_NOMEM` giữ datagram tới vòng sau, không giới hạn thời gian.
2. Mỗi lượt làm tối đa 8 frame ra và 8 lần đọc (4 KB mỗi chiều); lượt nào có làm việc thì kết thúc bằng `vTaskDelay(1)`, lượt rảnh thì block trong `select()` (≤ 10 ms).
   - Ca bench mới (9): 60 s tải liên tục cả hai chiều. Trong lúc đó task `io` vẫn giữ nhịp: 10 cửa sổ ADC mỗi giây, `late` +0, và xung DO 2 s vẫn đo được 2000 ± 20 ms.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
