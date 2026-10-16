Plan 09 code review r3. Các sửa cho 3 lỗi r2 nằm trong commit mới nhất của `plan09-mqtt`, mô tả ở mục "Code review round 2" của `stm32/docs/plans/2026-10-16-09-mqtt.md`; spec §8 cập nhật theo.

1. và 2. Đường "lấy mutex thất bại" trong `mqtt_sess` được bỏ hẳn:
   - task `mqtt` chờ tới khi lấy được mutex (`lock_wait` trong `mqttgw.c`, ghi log mỗi 2 s). Bên giữ mutex không bao giờ chờ gì, nên trạng thái của coreMQTT và của outbox không còn lệch nhau sau một PUBLISH hay một ack;
   - các task khác (`enqueue`, `add_listener`) vẫn giới hạn 2 s và bỏ qua thao tác khi hết hạn;
   - `test_lock_fails` bỏ đi cùng đường đó.
3. `mqtt_sess` giữ id của các SUBSCRIBE đang chờ SUBACK cho tới khi có SUBACK, hoặc tới hết kết nối; `next_id` bỏ qua chúng. `test_sub_wrap` RED (84 và 84) rồi GREEN.

Kiểm thử: host `test_mqtt_*` xanh; board `mqtt_check.py` (115) và `edge_check.py mqtt` đạt trên firmware mới.

Phạm vi: `git diff HEAD~1 HEAD -- stm32/firmware` (cả branch: `git diff 3ee0c0cea61aad9ed5d294030f44e0a76830c8bf HEAD -- stm32`). Trả lời APPROVED hoặc NOT APPROVED kèm lỗi Critical/Important.
