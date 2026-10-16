Plan 08c r2 (`stm32/docs/plans/2026-10-16-08c-edge.md`, commit 741e6fa) sửa 5 điểm của r1:

1. **Short connection và HTTP client:** report đang chờ trong slot mở kết nối giống như dữ liệu serial đang xếp hàng.
   - `report_accept` (có host test) chấp nhận TCP client có short connection và HTTP client ngay cả khi chưa có peer.
   - Nhánh HTTP lấy slot qua `take_frame`.
   - Bench có ca `shortreport`: không có traffic serial nào, report on-change tự dial rồi tới nơi.
2. **Mọi chỗ chạm vào frame hiện hành đều dùng `b->out`/`b->out_len`, kể cả `tx_finish` trong `reply_tcp`.** Bench `bigreport` có ca: client đọc chậm gửi request từ xa trong lúc report 6 KB đang gửi dở; kỳ vọng report tới nguyên vẹn rồi mới tới câu trả lời.
3. **Request dang dở:** byte bị giữ trong accumulator mà 200 ms không có byte mới thì đi tiếp đường 08a (như `edge_flush_stale` của ESP32).
   - `acc_stale` có host test.
   - Bench có ca `partial`.
4. **Khởi động all-or-nothing:**
   - `edge_start` dành trước mọi thứ: mô hình, template, buffer, mutex, semaphore, và stack cùng TCB của mỗi task (`pvPortMalloc`, để dùng `xTaskCreateStatic` vốn không thể thất bại).
   - Chỉ khi tất cả đều thành công mới đặt hook bridge và tạo task. Thất bại thì giải phóng hết, để trạng thái `NO MEMORY`, không task, không hook.
   - Phần dành trước là hàm thuần `edge_reserve`, có test làm hỏng lần lượt từng lần cấp phát; sau mỗi lần thất bại không còn block nào sống.
5. **`sys_unix_time` là số giây Unix;** `sys_time` là chuỗi UTC.
   - Cả hai không có giá trị (ra placeholder) khi chưa có giờ.
   - `edge_sys_value` có test cả giá trị lẫn kiểu dữ liệu.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi.
