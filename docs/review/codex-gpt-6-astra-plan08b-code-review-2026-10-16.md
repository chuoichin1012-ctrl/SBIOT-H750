NOT APPROVED

Còn **2 lỗi Important**:

1. **[bridge.c:505](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:505), [bridge.c:136](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:136): mất/lặp dữ liệu khi gửi TLS xen kẽ.**  
   Với TCP client TLS và `modbusPoll=true`, một frame serial đang chờ `WANT_WRITE` vẫn có thể bị chen bởi câu trả lời polling qua `conn_send_all`. Buffer và độ dài đã đổi, trái [hợp đồng `mbedtls_ssl_write`](https://github.com/Mbed-TLS/mbedtls/blob/mbedtls-3.6.7/include/mbedtls/ssl.h): thư viện có thể gửi nốt frame cũ nhưng báo đã gửi câu trả lời mới; câu trả lời mất, frame cũ bị gửi lặp.  
   **Sửa:** tuần tự hóa các đường ghi trên cùng phiên; thêm test polling đến khi frame serial còn chờ gửi.

2. **[tlsconn.c:153](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/tlsconn.c:153): bỏ qua lỗi `mbedtls_ssl_conf_own_cert`.**  
   Hàm này có thể trả `MBEDTLS_ERR_SSL_ALLOC_FAILED`. Nếu thiếu bộ nhớ thoáng qua rồi hồi phục, `tlsVerify=2` vẫn tiếp tục handshake mà chưa gắn chứng chỉ client; server cho phép chứng chỉ client tùy chọn có thể chấp nhận phiên đó.  
   **Sửa:** kiểm kết quả và đi vào `fail`; thêm fault injection tại lần cấp phát này.

Đã chạy đạt `test_tls_core`, `test_bridge_core`, `test_clock`, `test_web_core` với leak check tắt. Không xác minh được loopback `test_tlsconn` vì sandbox chặn tạo socket; không chạy lại bench.