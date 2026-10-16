**NOT APPROVED**

Cần sửa 4 điểm:

1. **Ruling 5: mbedTLS không tự bỏ SNI cho địa chỉ IP.** `set_hostname(IP)` vẫn gửi chuỗi IP trong SNI; kiểm IP SAN là việc riêng. Sửa diễn giải và chốt cách xử lý/giới hạn tương thích khi dial IP; không được bỏ kiểm danh tính server. Thêm test IP và DNS. Nguồn: [ssl_client.c 3.6.7](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.7/library/ssl_client.c).

2. **Đường `pending` phải sửa cả kết quả trả về của `wait()`** — [plan:233](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-16-08b-tls.md:233). Chỉ thêm fd vào `rd` chưa đủ: bridge hiện chỉ gọi `read_clients` khi `wait()` trả số dương. Khi socket im lặng nhưng TLS còn plaintext, phải báo readiness dương; nếu không dữ liệu bị treo và vòng timeout 0 có thể quay liên tục.

3. **Test gửi một record 12 KB không khả thi với OUT 4096** — [plan:425](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-16-08b-tls.md:425). Một lần `mbedtls_ssl_write` chỉ nhận tối đa kích thước record cho phép. Đổi ca pending thành 4 KB, vẫn lớn hơn lần đọc 1500 byte; hoặc cấu hình server riêng. Nguồn: [hợp đồng `mbedtls_ssl_write`](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.7/include/mbedtls/ssl.h).

4. **T0 bỏ lọt xung đột khi hết bộ nhớ** — [store.c:502](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:502). Parse trả `NULL` nhưng vẫn gọi `cfg_listen_ok`; `NULL` được hiểu thành không có listen port, nên cấu hình xung đột vẫn có thể được lưu. Phải từ chối khi không đọc/parse đủ ba cấu hình; thêm test lỗi cấp phát, xác nhận cấu hình đã lưu không đổi.

Các ruling còn lại chấp nhận được; cách tách giờ hiển thị khỏi giờ hệ thống phù hợp.