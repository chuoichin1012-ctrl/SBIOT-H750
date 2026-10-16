NOT APPROVED

Còn 3 lỗi **Important**, xác định qua luồng code:

1. **Mất trạng thái sau khi gửi nếu lấy mutex thất bại** — [mqtt_sess.c:36](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/mqtt_sess.c:36). `MQTT_Publish` đã gửi hoặc gửi dở và giữ packet ID trong coreMQTT, nhưng outbox vẫn `QUEUED` nếu lần khóa này thất bại. Khi reconnect giữ phiên, tin được gửi với ID mới, có thể giao trùng QoS 2 và để lại trạng thái cũ. Cần bảo toàn ID/trạng thái của lần gửi trước khi cho phép reconnect. Thêm fault injection **sau** `MQTT_Publish`; test hiện chỉ làm hỏng lần khóa đầu.

2. **Bỏ ACK khi mutex thất bại làm hai bảng trạng thái lệch nhau** — [mqtt_sess.c:115](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/mqtt_sess.c:115). coreMQTT cập nhật trạng thái **trước** callback. Nếu bỏ PUBCOMP, coreMQTT đã hoàn tất nhưng outbox còn `WAIT_PUBCOMP`; reconnect giữ phiên cũng không còn PUBREL để gửi lại, khiến record kẹt vĩnh viễn. Nếu bỏ PUBREC, PUBCOMP tiếp theo không khớp trạng thái outbox. Cần giữ ACK để áp dụng lại khi lấy được khóa; chỉ reconnect là chưa đủ. Thêm test lỗi khóa tại từng loại ACK.

3. **Packet ID chưa tránh SUBSCRIBE đang chờ SUBACK** — [mqtt_sess.c:8](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/mqtt_sess.c:8). `next_id` chỉ xét outbox; ID của SUBSCRIBE không được giữ. Khi SUBACK chậm và bộ đếm quay vòng, PUBLISH có thể dùng lại ID đó. Cần theo dõi ID chờ SUBACK và loại chúng khi cấp ID mới; thêm test wrap với SUBACK chưa nhận. Đây là yêu cầu của [MQTT §2.3.1](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html).

Đã chạy lại `test_mqtt_outbox` và `test_mqtt_wire`: **PASS**. Không chạy lại bench trên board.