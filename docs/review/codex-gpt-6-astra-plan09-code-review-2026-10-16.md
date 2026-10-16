NOT APPROVED

Còn **6 lỗi Important**:

1. **[mqtt_outbox.c:165](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/mqtt_outbox.c:165)** — Tin `WAIT_*` được đưa về `QUEUED` khi có phiên mới nhưng giữ nguyên `t_ms`. Nếu mất kết nối hơn một giờ, lần `ob_next_queued()` kế tiếp xóa tin trước khi gửi lại, trái yêu cầu không mất tin đang dở. Cần bắt đầu lại thời gian chờ cho tin được requeue; thêm test reconnect sau hơn một giờ.

2. **[mqttgw.c:479](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/mqttgw.c:479), [mqtt_sess.c:45](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/mqtt_sess.c:45)** — Chỉ nhường CPU khi transport thiếu dữ liệu/chỗ gửi. Khi broker gửi liên tục, hoặc outbox QoS 0 liên tục được bổ sung, vòng xử lý có thể chạy mãi; `mqtt_io` cũng không block nếu down-ring luôn có tin. Các task ưu tiên thấp như `edge_rpt`, syslog và idle bị đói. Cần giới hạn công việc mỗi lượt và nghỉ một tick; bench phải kiểm task thấp hơn MQTT, không chỉ ADC.

3. **[mqttgw.c:113](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/mqttgw.c:113)** — Hết hạn lấy mutex chỉ ghi log rồi tiếp tục. Bên gọi vẫn sửa outbox/listener và gọi `unlock()` dù không sở hữu mutex, có thể làm hỏng ring hoặc gây assert. Cần trả kết quả lấy khóa và dừng thao tác khi thất bại; thêm fault injection cho timeout.

4. **[mqtt_sess.c:11](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/mqtt_sess.c:11)** — `MQTT_GetPacketId()` quay vòng sau 65535 và không tránh ID còn đang dùng. Sau nhiều reconnect giữ phiên, ID mới có thể trùng giao dịch cũ; dòng 19 còn biến `MQTTStateCollision` thành xóa tin rồi báo thành công. Cần cấp ID chưa dùng cho cả PUBLISH/SUBSCRIBE và không coi lỗi trạng thái là cấu hình sai để bỏ tin. Thêm test wrap với ID còn chờ ACK. [coreMQTT](https://raw.githubusercontent.com/FreeRTOS/coreMQTT/v2.3.1/source/core_mqtt.c), [MQTT §2.3.1](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html).

5. **[mqttgw.c:160](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/mqttgw.c:160)** — `store_get()` thất bại vì OOM bị hiểu thành danh sách topic rỗng. MQTT vẫn khởi chạy và báo CONNECTED nhưng mất toàn bộ publish hoặc subscribe tới lần reboot sau. Cần phân biệt lỗi đọc/parse với mảng rỗng hợp lệ, dừng khởi động với trạng thái lỗi và kiểm bằng fault injection.

6. **[test_mqtt_session.c:168](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/host/test_mqtt_session.c:168)** — `DROP_MID_PUBLISH` đóng socket **sau khi `packet()` đã đọc hết payload**. Ca này chưa kiểm đường `MQTTSendFailed` giữa PUBLISH. Cần ép transport lỗi sau một phần payload, assert lần publish thực sự thất bại, rồi kiểm gửi lại đầy đủ cho cả phiên giữ và phiên mới.

Chạy lại đạt: `test_mqtt_wire`, `test_mqtt_outbox`, `test_edge_core`. Hai test dùng socket không xác minh được trong sandbox vì gửi bị chặn `EPERM`; không coi đó là lỗi firmware. Bench trên board chưa chạy lại.