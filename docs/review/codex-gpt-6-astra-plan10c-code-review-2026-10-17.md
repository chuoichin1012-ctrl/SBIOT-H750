NOT APPROVED

Còn 2 lỗi **Important**:

1. **[serial.c:262](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/serial.c:262) — vẫn có thể phát request sau deadline.**  
   Với `packetInterval=255`, timeout `100 ms`, vừa nhận byte khi bắt đầu chờ: vòng lặp ngủ 256 ms. Khi thức dậy, nếu bus đã im lặng, nó thoát trước khi kiểm deadline rồi phát request. Ngoài ra, vòng này tạo deadline mới, không dùng chung với vòng chờ `tail`.  
   **Sửa:** một deadline cho cả hai vòng; kiểm hết hạn trước khi chấp nhận bus đã im lặng; giới hạn thời gian ngủ bằng thời gian còn lại.

2. **[edgecore.c:396](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/proto/edgecore.c:396) — placeholder vẫn có thể làm report sai JSON.**  
   `cJSON_ParseWithOpts(..., true)` vẫn chấp nhận số không đúng chuẩn như `01` hoặc `1.`. UI cho nhập các chuỗi này; khi điểm đọc lỗi, firmware chép nguyên placeholder, tạo report như `{"x":01}`, khiến bên nhận JSON từ chối.  
   **Sửa:** dùng scanner `js_parse_root` hiện có để phân loại literal đúng cú pháp; nếu không hợp lệ thì xuất thành chuỗi. Thêm test `01` và `1.`.

Đã chạy lại các binary host hiện có: `test_proto`, `test_web_core`, `test_bridge_core` đều đạt; chưa chạy board trong lượt review này.