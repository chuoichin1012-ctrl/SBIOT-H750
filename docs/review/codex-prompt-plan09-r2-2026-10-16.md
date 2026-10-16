Plan 09 r2 (commit def2b7e) sửa 4 điểm của r1:

1. **Một context duy nhất** (ruling 3): `MQTT_Init` và `MQTT_InitStatefulQoS(16,16)` chỉ chạy một lần lúc start. Khi kết nối lại thì chỉ thay connection và gọi `MQTT_Connect`, nên packet id không bao giờ quay về 1 khi còn tin đang chờ ack.
   - PUBLISH lỗi giữa chừng vẫn chuyển record sang `WAIT_*` với id đó: phiên được giữ thì gửi lại kèm DUP với cùng id; phiên mới thì dùng id mới.
   - Test: ngắt giữa một PUBLISH khi còn tin xếp hàng phía sau; packet id không reset qua các lần kết nối lại.
2. **PUBLISH đến quá lớn** (ruling 11 mới): bản vá coreMQTT, với một PUBLISH bị bỏ mà header, topic và packet id còn nằm vừa trong buffer, chạy đúng `MQTT_UpdateStatePublish` và `sendPublishAcks` như `handleIncomingPublish`.
   - QoS 1 nhận PUBACK; QoS 2 nhận PUBREC, rồi PUBCOMP khi có PUBREL. Payload bị bỏ, có cảnh báo, và không có event nào lên ứng dụng.
   - Nếu không đọc được cả topic thì đóng kết nối và ghi log.
   - Test: `test_mqtt_session` với QoS 1/2, rồi một tin bình thường đi sau; bench `oversized` dùng mosquitto với `max_inflight_messages 1`.
3. **Listener thêm sau khi đã kết nối** (ruling 9 mới): `mqttgw_add_listener` thêm entry dưới mutex rồi notify task `mqtt`, task này SUBSCRIBE ngay nếu đang kết nối.
   - Danh sách thuần `subs` giữ cờ "đã subscribe trong phiên này" cho từng entry; mỗi lần connect thì subscribe lại tất cả.
   - Test: thêm listener sau CONNECTED, không kết nối lại.
4. **Chống quay vòng** (ruling 2):
   - Chỉ đường đọc mới được bỏ qua bước chờ khi `tls_pending`; đường gửi gặp `NET_AGAIN` luôn chờ cho tới khi ghi được.
   - Hai kết quả liên tiếp không tiến triển thì nghỉ một tick (`vTaskDelay(1)`), kể cả khi `select` trả về ngay.
   - Test: wait trả về ngay; gửi gặp AGAIN khi `pending` đang true.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi.
