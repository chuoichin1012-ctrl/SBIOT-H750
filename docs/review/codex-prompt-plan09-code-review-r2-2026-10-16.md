Plan 09 code review r2. Các sửa cho 6 lỗi r1 nằm trong commit mới nhất của `plan09-mqtt` (sau 36ef8a2), mô tả ở mục "Code review round 1 (NOT APPROVED), and the fixes" của `stm32/docs/plans/2026-10-16-09-mqtt.md`; spec §8 cập nhật theo.

1. `ob_session(kept, now)`: chỉ tin `WAIT_*` được requeue, tính lại hạn 1 giờ. Khi sửa còn tìm thêm một lỗi: requeue trước đây hồi sinh cả record đã xóa mà chưa thu hồi (tin QoS 0 hay đã ack nằm sau một tin chậm). `test_requeue` RED rồi GREEN.
2. Hai task nghỉ 1 tick sau mỗi 8 vòng; `ms_feed` gửi tối đa 8 tin mỗi lần gọi. Không tái hiện được tình trạng đói trên bench: trên firmware cũ, 182 539 tin qua relay (khoảng 18k/s) không làm đói `serial_hb`. Case bench `flood` giữ lại để canh.
3. `lock()` trả bool; khi không lấy được mutex, mọi bên gọi đều không làm gì (`MQTTIllegalState` thì kết nối lại). `test_lock_fails` RED rồi GREEN.
4. `next_id` bỏ qua id đang được giữ (`ob_id_used`), cho cả PUBLISH và SUBSCRIBE. Chỉ `MQTTBadParameter` làm bỏ tin. `test_id_wrap` RED rồi GREEN.
5. `store_get` NULL (thiếu bộ nhớ) thì MQTT tắt với `NO MEMORY`; chỉ kiểm bằng đọc code.
6. `DROP_MID_PUBLISH` đọc 2000 byte rồi đóng; test assert `MQTTSendFailed` và id được giữ. Một mutation (bỏ tin khi `MQTTSendFailed`) làm test hỏng.

Kiểm thử: host `test_mqtt_outbox`, `test_mqtt_session`, `test_mqtt_wire`, `test_mqtt_transport` xanh; board: `mqtt_check.py` 115 kiểm tra cùng `edge_check.py mqtt` đạt trên firmware mới.

Phạm vi: `git diff 36ef8a2 HEAD -- stm32` (cả branch: `git diff 3ee0c0cea61aad9ed5d294030f44e0a76830c8bf HEAD -- stm32`). Trả lời APPROVED hoặc NOT APPROVED kèm lỗi Critical/Important.
