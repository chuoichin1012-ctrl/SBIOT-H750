NOT APPROVED

Còn 5 điểm **Important** cần sửa:

1. **[edge_rt.c:808](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/edge_rt.c:808)** — `store_edge_text()` trả 0 khi không đọc được model, nhưng bị thay bằng `[]`. Khi DB Edge lỗi còn cấu hình vẫn đọc được, Edge có thể báo `RUNNING 0/0` và gửi report rỗng. Trái §9.1: trường hợp này phải tắt Edge. Cần trả trạng thái `MODEL UNREADABLE`, không dựng model thay thế.

2. **[edge_rt.c:866](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/edge_rt.c:866), [edge_rt.c:972](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/edge_rt.c:972), [api.c:247](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/api.c:247)** — Lỗi cấp phát khi parse/compile template bị coi như template không hợp lệ: lúc boot vẫn khởi chạy Edge nhưng mất report; lúc lưu có thể bỏ qua kiểm giới hạn 26 KB. Cần phân biệt OOM: boot giải phóng và báo `NO MEMORY`; API từ chối lưu, giữ cấu hình cũ. Thêm fault injection vào chính các đường này.

3. **[edge_core.c:102](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/edge_core.c:102)** — Biến `e` 16 bit làm mốc kết thúc 65536 tràn thành 0. Ví dụ bật merge, có `uint16` tại 65530 và `float32` tại 65534: batch chứa hai điểm nhưng chỉ đọc một thanh ghi; điểm thứ hai luôn lỗi. Dùng kiểu rộng hơn cho `end/e`, thêm test sát biên 65536.

4. **[bridge.c:334](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:334)** — Cổng Modbus gateway bỏ qua ghép frame RTU của Edge. Request TCP bị chia thành hai đoạn hoặc ghép nhiều request sẽ không được xử lý đúng, dù cấu hình này được chấp nhận. Ruling “xử lý từng segment” làm mất hành vi đã duyệt. Cần ghép RTU đầy đủ trước khi chuyển phần không thuộc Edge xuống gateway.

5. **[edge_check.py:667](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/bench/edge_check.py:667)** — Ca `bigreport` chưa chứng minh request tới khi report còn gửi dở. Report khoảng 3,2 KB, client đọc 512 byte mỗi 10 ms; assertion cuối vẫn đạt khi report đã gửi xong. Cần ép và xác nhận trạng thái gửi dở trước khi gửi request, rồi kiểm report nguyên vẹn trước câu trả lời.

Các host test chạy lại đều đạt, gồm đối chiếu 8.090 payload. Tôi không chạy lại bench trên board.