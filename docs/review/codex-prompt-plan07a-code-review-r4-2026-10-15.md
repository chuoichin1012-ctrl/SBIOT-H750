Đã sửa 2 lỗi Important của review code 07a vòng 3 (commit mới nhất trên `plan07-serial-io`; mục "Code review round 3" cuối plan). Xin **review lại**: `git diff a094ce4..HEAD -- stm32/`.

1. `serial_transact` chờ `tail` kết thúc trước khi làm gì khác (trong giới hạn timeout của nó; quá hạn thì trả 0, không gửi gì). Chỉ `serial_rx[n]` xóa `tail`, và trong lúc transaction giữ bus thì không ai đặt lại nó. Bench `tail`: transaction thứ hai gửi trong lúc đuôi đang chạy; peer nhận nguyên cả hai request, request thứ hai bắt đầu muộn hơn khoảng t3.5 so với trước khi sửa (1267 ms so với 1234 ms). Trên rig này ca này pass cả trước khi sửa, vì bộ thu tắt khi cổng đang phát nên va chạm không lộ ra; nó chỉ chặn hồi quy.
2. Cuối transaction, `tail = p->rx.resync` với mọi kết quả (vi phạm, lỗi đường truyền, timeout). Bench `tail` (UART1 7E1, UART2 8N1, chỉ UART1 thấy lỗi parity): lỗi parity, đuôi chạy quá timeout 1 s, nghỉ 20 ms, thêm 2 byte. Trước khi sửa, 2 byte lọt sang subscriber; sau khi sửa thì không.

**Kết quả:** `serial_check.py` 35/35 trên board (chỉ `serial.c` đổi trong vòng này, nên không chạy lại web/store).

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa. Nhớ người dùng muốn giữ đơn giản.
