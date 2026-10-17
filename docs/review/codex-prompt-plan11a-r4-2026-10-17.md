Plan 11a, vòng 4. Người dùng thêm một yêu cầu: một task giám sát các task khác (2026-10-17). Đã thêm Task 0b vào `stm32/docs/plans/2026-10-17-11a-identity-and-gates.md`; các task khác giữ nguyên như bản bạn đã duyệt ở r3. Task 0 (nhịp LED) đã làm xong, và trên board đo được `iwdgMaxGapMs` 50 ms, `wdiMaxGapMs` 500 ms.

**Task 0b:**
- **Cơ chế:** heartbeat, không kick từ idle task, vì idle có thể bị đói một cách hợp lệ: TLS, FlashDB dọn rác, flood LAN.
- **`hb.c`:** logic thuần, có test host, tối đa 24 slot.
- **Task được giám sát:** `tcpip`, `eth_rx`, `web`, `console`, `bridge`, `mqtt`, Edge, `lte`, `cmux_rx`, serial RX, `io`, `syslog`. Mỗi task có một timeout rộng (10–180 s); dịch vụ nào tắt lúc boot thì không đăng ký.
- **Xử lý khi quá hạn:** `wdog` kiểm mỗi 1 s. Task nào quá hạn thì ghi log tên task, chờ 1 s cho log xuống flash, rồi ngừng kick để IWDG1 reset board. Không có cơ chế phục hồi nào khác.
- **Bench:** lệnh console `I` làm task `web` dừng 40 s; kỳ vọng board reset và log sau khi boot ghi đúng tên task.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa. Người dùng muốn giữ đơn giản.
