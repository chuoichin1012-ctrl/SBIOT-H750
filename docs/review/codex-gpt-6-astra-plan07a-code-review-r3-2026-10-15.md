NOT APPROVED

Còn hai lỗi **Important**:

1. [serial.c:251](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/serial.c:251): transaction mới chỉ chờ `gap_ms + 1`, rồi xóa `tail`; vào RTU lại xóa `resync`. Với 1200 8E1, `packetInterval=1`, transaction kế tiếp có thể phát trong khoảng nghỉ của đuôi lỗi **chưa đủ t3.5**, gây va chạm hoặc nhận nhầm đuôi. Cần chờ resync kết thúc trước khi bắt đầu transaction; thêm ca transaction thứ hai ngay trong đuôi lỗi.

2. [serial.c:275](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/serial.c:275): chỉ giữ timing RTU khi kết quả là `SERIAL_DROPPED`. Lỗi đường truyền cũng đặt `resync`, nhưng nếu transaction timeout thì vẫn chuyển sang transparent; khoảng nghỉ ngắn có thể kết thúc resync sớm. Cần giữ RTU khi resync còn hoạt động, kể cả đường timeout; thêm test tương ứng.

Cơ chế giành quyền restart đã xử lý đúng race với wipe. Binary host `test_serial_frame` hiện có chạy đạt; tôi không chạy lại board.