NOT APPROVED

Cần sửa 5 điểm trong plan:

1. **Ruling 1 — report không mở được short connection.** `bridge_send` từ chối khi `online=false`, trong khi [bridge.c:489](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:489) chỉ kết nối khi hàng đợi serial có dữ liệu. Report đơn độc sẽ không bao giờ gửi. Cho slot report kích hoạt kết nối; thêm test không có traffic serial. Nhánh HTTP cũng phải lấy slot, thay vì chỉ đọc MessageBuffer.

2. **T3 — chuyển sang `out/out_len` phải bao gồm `reply_tcp`.** [bridge.c:158](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:158) vẫn truyền `frame/frame_len` vào `tx_finish`. Nếu report đang gửi dở rồi có request từ xa, dùng buffer này sẽ tái tạo lỗi retry TLS của 08b. Ghi rõ thay đổi và thêm test **report lớn đang chờ gửi + câu trả lời từ xa**.

3. **Ruling 3 — thiếu xử lý frame dang dở.** Một đoạn passthrough trông giống tiền tố RTU/MBAP có thể bị giữ mãi khi không có byte tiếp theo. Giữ cơ chế timeout chuyển tiếp như [ESP32 bridge.c:330](/home/linh/projects/SBIOT-Gateway/firmware/components/services/bridge.c:330); thêm test tiền tố dang dở rồi im lặng.

4. **Ruling 2 / `load_oom` — chưa kiểm hết cấp phát mới.** Sweep cJSON không bao phủ model, template, stack, mutex và `xTaskCreate`. Bổ sung quy tắc khởi động thất bại không để lại task/hook Edge hoạt động một phần, cùng fault injection các cấp phát này. Không chép nguyên đường khởi động ESP32: nó có thể thất bại sau khi đã tạo một số task.

5. **Ruling 5 — `sys_unix_time` phải là số giây Unix**, không qua `clock_format`; chỉ `sys_time` là chuỗi UTC. Baseline xác định rõ tại [edge_rt.c:857](/home/linh/projects/SBIOT-Gateway/firmware/components/services/edge_rt.c:857). Thêm test giá trị và kiểu dữ liệu, cả khi chưa đặt giờ.

Các lựa chọn còn lại của sáu ruling có thể chấp nhận.