Plan 08b r2 (`stm32/docs/plans/2026-10-16-08b-tls.md`, commit ac2a82d) sửa 4 điểm của r1:

1. **SNI với địa chỉ IP (ruling 5):** đã sửa phần diễn giải. mbedTLS gửi nguyên chuỗi IP trong SNI, và 3.6 không có tùy chọn nào giữ việc kiểm tên mà chỉ bỏ SNI.
   - Không bao giờ bỏ kiểm danh tính server: không dùng `set_hostname(NULL)`.
   - Ghi rõ giới hạn tương thích: OpenSSL, Python, nginx và Mosquitto chấp nhận SNI là IP. Server nào từ chối thì phải dial bằng tên. Spec §7.3 ghi lại giới hạn này.
   - `test_tlsconn` có ca **names**: `localhost`/`DNS:localhost` và `127.0.0.1`/`IP:127.0.0.1` đều thành công, server ghi lại SNI nhận được; tên hoặc IP không có trong SAN thì thất bại với `CN_MISMATCH`.
2. **`wait()`:** kết quả trả về giờ tính cả các client còn dữ liệu nằm trong mbedTLS, qua `wait_ready(selected, pending)` trong `bridge_core`, có test trong `test_bridge_core` (gồm cả trường hợp `(0,1)` và `(-1,1)` ra 1).
3. **Ca pending dùng 4 KB:** một record, đúng giới hạn OUT 4096, vẫn lớn hơn mỗi lần đọc 1500 byte.
4. **T0 khi hết bộ nhớ:** đã sửa trong commit 6289802.
   - `listen_check` trả `STORE_ENOMEM` khi một trong ba config có mặt nhưng parse không được.
   - `test_store` lần lượt làm hỏng từng lần cấp phát của cJSON trong một lần lưu bị xung đột, đếm qua `json_alloc_failures` giống bộ cấp phát của board. Test xác nhận lần lưu không bao giờ được chấp nhận và config đã lưu không đổi.
   - Đã thấy test đỏ khi chưa sửa và xanh sau khi sửa; `STORE_CUTS` và `test_store_small` cũng xanh.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
