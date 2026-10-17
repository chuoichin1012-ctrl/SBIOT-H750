NOT APPROVED

Có 4 lỗi **Important**:

1. **HEAD chưa chứa đủ Task 0b.** [app_main.c:222](/home/linh/projects/SBIOT-Gateway/stm32/firmware/app/app_main.c:222), [CMakeLists.txt:118](/home/linh/projects/SBIOT-Gateway/stm32/firmware/CMakeLists.txt:118): HEAD `64fda78` gọi `wdog_reason_take`, `hb_restart_was_ours`, `web_bench_hang`, nhưng phần định nghĩa/tích hợp tương ứng vẫn chưa commit. Checkout sạch revision này không build được. **Sửa:** đưa đầy đủ phần tích hợp vào commit rồi build và chạy gate trên revision đó.

Các lỗi dưới đây nằm trong phần tích hợp ở working tree:

2. **Bridge đã báo ERROR vẫn gây reset định kỳ.** [bridge.c:907](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:907), [bridge.c:924](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:924): task đăng ký heartbeat trước khi mở socket. Nếu `bind/listen` thất bại, nó chuyển sang ngủ vô hạn, khiến toàn gateway reset sau 600 s. Ví dụ hai listener trùng cổng sẽ lặp lại lỗi sau mỗi boot. **Sửa:** bỏ giám sát khi task kết thúc ở trạng thái ERROR, hoặc giữ vòng ngủ có heartbeat cho trạng thái này.

3. **HTTP report hợp lệ có thể làm `edge_rpt` bị coi là treo.** [edge_rt.c:796](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/edge_rt.c:796), [bridge.c:986](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:986): heartbeat chỉ có ngoài `report()`, còn `bridge_send()` chờ tới khi bridge hoàn tất. Với `httpTimeout=255`, server nhận request nhưng chưa trả lời sau 120 s thì watchdog reset, dù bridge vẫn đang chờ đúng deadline. **Sửa:** beat cho task gọi giữa các lát chờ hoàn tất report; giữ nguyên quyền sở hữu buffer và deadline.

4. **Ghi nhiều điểm Edge qua MQTT có thể reset giữa chừng.** [mqttgw.c:357](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/mqttgw.c:357), [edge_rt.c:542](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/edge_rt.c:542): `mqtt_io` gọi xử lý Edge đồng bộ; FC16 ghi từng điểm mà không beat giữa các lần ghi. Ví dụ 123 điểm, mỗi phản hồi mất 1,1 s với timeout 2 s: tác vụ vẫn tiến triển nhưng vượt 120 s, gây reset sau khi mới ghi một phần. **Sửa:** beat cho **task đang xử lý request** giữa các thao tác hoàn tất, không beat khi chờ khóa; kiểm cả đường bridge gọi cùng logic.

Các kịch bản trên được xác minh bằng luồng code; tôi chưa tái hiện trên board.