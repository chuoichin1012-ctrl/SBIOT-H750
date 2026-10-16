Plan 08b, code review r2: commit ba6067e sửa 2 lỗi của r1.

1. **Câu trả lời chen vào giữa frame đang gửi.**
   - `tx_finish(tx, frame, len, send, ctx, now_ms, wait, limit_ms)` mới trong `bridge_core` gửi nốt frame đang dở bằng cùng dữ liệu (đúng hợp đồng `mbedtls_ssl_write`) trước khi gửi gì khác trên kết nối đó, trong giới hạn 2 s của một câu trả lời. Quá hạn thì bỏ kết nối.
   - `reply_tcp` gọi `tx_finish` khi `cl->pend && cl->tx.busy`, rồi mới `conn_send_all` câu trả lời. `ctx` của nó giờ là `{b, cl}`.
   - Heartbeat vốn chỉ gửi khi `!b->have`. Registration chỉ gửi lúc mới kết nối. Vì vậy chỉ còn đường trả lời có thể chen vào frame.
   - Test `test_bridge_core` (đã thấy đỏ rồi xanh): luồng byte ra đúng thứ tự frame rồi đến câu trả lời; không có frame đang gửi thì không gửi gì; peer không nhận byte nào thì mất kết nối sau 2 s.
   - Bench đạt: `bridge_check.py tcpserver modbus mbclient` (có polling) và `tls_check.py tcpclient pending https`.
2. **Bỏ qua lỗi của `mbedtls_ssl_conf_own_cert`.**
   - Giờ đã kiểm kết quả và đi vào `fail`.
   - `test_tlsconn` có ca `mutual_oom`: dùng `-Wl,--wrap=calloc`, lần lượt làm hỏng từng lần `calloc` trong thread client (6389 lần), với server dùng `VERIFY_OPTIONAL`.
   - Trước khi sửa, ca này tìm ra đúng 1 phiên lên mà không có chứng chỉ client; sau khi sửa thì không còn phiên nào như vậy.

Các host test bị ảnh hưởng đều xanh.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi.
