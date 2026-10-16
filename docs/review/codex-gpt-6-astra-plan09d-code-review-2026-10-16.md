NOT APPROVED

**Important — tràn stack khi một thiết bị có 300 điểm.**  
[edge_rt.c:357](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/edge_rt.c:357): `order[]` và `batches[]` cần **300 × (4 + 20) = 7.200 byte**, chưa tính các hàm gọi tiếp. Trong khi stack task RTU chỉ **6.144 byte**, TCP **5.120 byte**.

Cấu hình hợp lệ dồn 300 điểm vào một thiết bị có thể làm hỏng bộ nhớ hoặc reset ngay khi polling. Bench hiện chia điểm giữa nhiều thiết bị nên chưa bắt được.

**Cần sửa:** bố trí đủ stack hoặc chuyển hai buffer khỏi stack; thêm bench một thiết bị RTU 300 điểm và một thiết bị TCP 300 điểm, kiểm polling và headroom stack.

Đã chạy lại các binary host hiện có: `test_spi_nor`, `test_cfgmodel`, `test_edge_text` đều đạt. Chưa chạy lại board.