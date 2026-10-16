NOT APPROVED

Còn **1 lỗi Important**:

- **[bridge.c:996](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:996)** — Kết nối được thiết lập **trước khi `bridge_set_edge()` chạy** giữ `cl->eacc == NULL` suốt phiên. Đây là đường chạy có thật: `services_start()` khởi chạy bridge trước khi nạp Edge. Sau đó, request RTU/MBAP bị chia đoạn vẫn rơi vào xử lý từng segment tại dòng 337–342 và không được ghép đúng, cho tới khi kết nối lại.

  **Sửa đơn giản:** trong task bridge, gắn accumulator còn thiếu trước khi xử lý dữ liệu khi Edge đã sẵn sàng. Thêm test: kết nối trước khi bật hook, giữ nguyên socket, rồi gửi request chia đoạn và nhiều request ghép chung.

Các sửa đổi còn lại không thấy lỗi chặn mới. `test_edge_core` và `test_bridge_core` chạy lại đạt; tôi không chạy lại bench trên board.