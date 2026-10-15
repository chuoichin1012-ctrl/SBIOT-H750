QUYẾT ĐỊNH: NOT APPROVED

**Important:** [provision.py:79](/home/linh/projects/SBIOT-Gateway/stm32/firmware/tools/provision.py:79), tương tự dòng 82, mở đầu ra bằng `wb` **trước khi đọc khóa**. Lệnh `pubkey prod.key prod.key` hoặc `record prod.key UID prod.key` xóa trắng khóa riêng rồi mới báo lỗi. Đã tái hiện bằng giả lập I/O trong RAM.

**Cần sửa:** từ chối đầu ra trùng tệp khóa, kể cả symlink/hardlink; đọc và kiểm đầu vào trước khi mở đầu ra. Thêm test cả hai lệnh, xác nhận khóa giữ nguyên khi bị từ chối.

**Năm ruling:**

- **1, 2, 4:** chấp nhận; cách gọi kiểm Ed25519 phù hợp [API Monocypher](https://monocypher.org/manual/ed25519).
- **3:** chấp nhận hành vi, nhưng sửa diễn giải: CRC không chứng minh được “hư hỏng hay giả mạo”; đây chỉ là quy tắc phân loại lỗi.
- **5:** chấp nhận trong phạm vi dev và quy trình sản xuất đã nêu; khóa dev không cung cấp chống clone.

Chưa thấy thêm đường vượt kiểm tra hoặc xóa nhầm ngoài các trường hợp đã công khai trong spec. Hiệu lực trên board vẫn phải được kiểm khi tích hợp ở **04b**, gồm UID thật, khóa production và thứ tự kiểm trước OTA/Reload.

Binary `test_bind` hiện có chạy **đạt**. Chưa build lại hoặc chạy lại toàn bộ suite/mutants.