NOT APPROVED

Còn hai lỗi **Important** trong kiểm tra UI:

1. **Địa chỉ thiết bị rỗng vẫn qua Done và Save.**  
   [edgeModel.ts:60](/home/linh/projects/SBIOT-Gateway/.claude/worktrees/review-fixes/stm32/web/src/lib/edgeModel.ts:60)  
   `addr` chưa có `required`, nên `fieldError()` trả về trước khi chạy kiểm tra địa chỉ. Xóa địa chỉ của thiết bị Modbus rồi lưu vẫn được; runtime chuyển chuỗi rỗng thành unit 0, khiến việc polling sai. Probe `modelErrors()` với `addr=""` trả `[]`.

   **Sửa:** thêm `required: true` cho `addr`; giữ ngoại lệ trường IO bị disabled. Test chuỗi rỗng và toàn khoảng trắng.

2. **Kiểm tra toàn mô hình bỏ sót tên điểm trùng.**  
   [edgeModel.ts:195](/home/linh/projects/SBIOT-Gateway/.claude/worktrees/review-fixes/stm32/web/src/lib/edgeModel.ts:195)  
   `names.filter(x => x !== n.name)` loại **mọi** điểm cùng tên, thay vì chỉ điểm đang kiểm. Mô hình đã lưu có hai điểm tên `same` vẫn qua kiểm tra Save; sau reboot, `cfg_edge_load` từ chối và Edge không chạy. Probe hai thiết bị có điểm trùng tên trả `[]`.

   **Sửa:** kiểm tên trùng bằng một `Set` trên toàn mô hình, hoặc loại điểm hiện tại theo vị trí. Thêm test trùng trong cùng thiết bị và giữa hai thiết bị.

Đây là các lỗ hổng còn lại của kiểm tra toàn mô hình, không cần thêm xử lý cấu hình trong firmware. Chưa xác minh serial/MQTT trên board.