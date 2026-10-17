NOT APPROVED

Còn một lỗi **Important**:

- [mqttgw.c:357](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/mqttgw.c:357), [edge_rt.c:458](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/edge_rt.c:458): `mqtt_io` vẫn có timeout heartbeat 120 s, nhưng beat chỉ trước toàn bộ `write_point()`. Với timeout điểm hợp lệ 65.535 ms, một lần ghi có thể chờ bus 60 s rồi chờ phản hồi thêm 65,5 s (`serial.c:248,296`). Watchdog reset giữa một giao dịch vẫn đang trong thời hạn.

  **Sửa đơn giản:** nâng timeout của `mqtt_io` để bao trọn một giao dịch, chẳng hạn 400 s như task poll Edge; giữ beat giữa các điểm và không beat lúc chờ khóa. Thêm ca kiểm chờ bus cộng chờ phản hồi vượt 120 s.

Ba điểm còn lại đã được xử lý. Kịch bản trên xác minh bằng code, chưa tái hiện trên board.