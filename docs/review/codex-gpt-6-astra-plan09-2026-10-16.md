NOT APPROVED

Cần sửa rõ 4 điểm trong plan:

1. **Vòng đời phiên — Architecture, dòng 173.** Không đặt `MQTT_Init` trong mỗi lần reconnect: nó reset bộ cấp packet ID về 1, có thể đụng ID đang chờ ACK. Khởi tạo context và hai bảng QoS một lần; reconnect chỉ thay transport và gọi `MQTT_Connect`. Bổ sung cách đồng bộ outbox khi gửi PUBLISH lỗi giữa chừng, cùng test reconnect còn tin đang dở và có tin mới. [Nguồn coreMQTT 2.3.1](https://raw.githubusercontent.com/FreeRTOS/coreMQTT/v2.3.1/source/core_mqtt.c).

2. **PUBLISH quá lớn — Review Focus 4.** coreMQTT bỏ packet trước khi xử lý QoS, nên không gửi PUBACK/PUBREC. Với QoS 1/2, broker có thể hết cửa sổ gửi và ngừng chuyển các tin tiếp theo dù kết nối vẫn mở. Cần quy định cách loại payload nhưng hoàn tất giao dịch QoS; test cả QoS 1/2 với cửa sổ broker nhỏ, sau đó gửi tin hợp lệ. Chỉ kiểm một retained message QoS 0 chưa đủ. [Nguồn coreMQTT](https://raw.githubusercontent.com/FreeRTOS/coreMQTT/v2.3.1/source/core_mqtt.c).

3. **Listener Edge thêm sau connect — ruling 9.** Cần xử lý trường hợp MQTT đã kết nối trước khi `edge_start` gọi `mqttgw_add_listener`. Đơn giản nhất: thêm listener rồi notify task `mqtt` để SUBSCRIBE ngay; callback chỉ chạy khi Edge sẵn sàng. Test thêm listener sau CONNECTED, không reconnect. ESP32 đã có đường đăng ký ngay khi thêm listener lúc đang kết nối.

4. **Chống quay vòng — ruling 2.** Gọi `select()` không đồng nghĩa đã nhường CPU: nó có thể trả ngay. `tls_pending` chỉ được bỏ chờ ở đường đọc; không được làm đường gửi `NET_AGAIN` quay liên tục khi TLS đang giữ dữ liệu nhận. Quy định nghỉ một tick khi liên tiếp không tiến triển dù `select` trả ngay, và thêm test cho trường hợp này.

Các lựa chọn còn lại có thể giữ, gồm outbox tại chỗ, phân vùng RAM và IO control chỉ RTU.