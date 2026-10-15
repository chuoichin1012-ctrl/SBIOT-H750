**NOT APPROVED**

Cần chốt ba điểm trong plan:

1. **Hành vi khi gửi thiếu bộ nhớ mâu thuẫn** — [dòng 78 và Review Focus 1](stm32/docs/plans/2026-10-16-08a-bridge.md#L78): vừa “không đóng socket”, vừa đóng sau 5 s. Spec §5.6 yêu cầu giữ dữ liệu và socket khi thiếu bộ nhớ. Cần phân biệt lỗi tạm thời với lỗi kết nối; giữ frame và offset đã gửi, retry có chờ để không làm đói task `io`. Thêm test gửi một phần rồi `ENOMEM`/`ENOBUFS`: không mất/lặp byte, không đóng socket.

2. **Ghi rõ chuyển API đóng socket khi port `net.c`.** ESP32 dùng `close(fd)`, nhưng STM32 đặt `LWIP_POSIX_SOCKETS_IO_NAMES=0`; lời gọi đó không chuyển thành `lwip_close()`. Phải dùng `lwip_close()`/`closesocket()` ở cả đường lỗi và đóng bình thường, tránh rò socket sau các lần reconnect.

3. **Bổ sung gate nhận kết nối của §5.6:** listener ngừng `accept` khi pool netconn còn dưới 4. Hiện plan chỉ kiểm `maxClients`, chưa có điều kiện này.

**Năm ruling đều chấp nhận được.** Việc chuyển stack bridge sang AXI cũng hợp lý, nhưng cần cập nhật §5.3 cùng ngân sách RAM khi triển khai.