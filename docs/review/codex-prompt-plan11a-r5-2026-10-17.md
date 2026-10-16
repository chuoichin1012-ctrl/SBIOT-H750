Plan 11a, vòng 5 (Task 0b). Đã sửa cả 3 điểm của vòng 4:

1. **Chờ hợp lệ và bị kẹt:**
   - chờ dữ liệu và ngủ theo lịch được chia thành các lát, mỗi lát tối đa 1/3 timeout, có beat giữa các lát, và giữ nguyên deadline nghiệp vụ;
   - chờ lock (mutex hay semaphore bảo vệ trạng thái dùng chung: `lock_wait` của mqttgw, cache Edge, bus serial) không bao giờ beat;
   - test trên board: MQTT với `retryInterval` 300 và broker không tới được, chạy 7 phút không reset; còn lệnh `I` (web dừng 40 s) thì reset.
2. **Đồng thời:**
   - `hb_add` chạy trong critical section (`wdog_hb_add`); tên task được sao chép vào `name[16]`; slot chỉ được công bố bằng atomic release sau khi đã khởi tạo xong;
   - timestamp đọc và ghi bằng atomic;
   - `HB_MAX` là 32, trong khi tối đa chỉ 18 task cùng chạy;
   - hết slot thì ghi log và tăng `hbMissing` trong `/api/status` (bench kiểm giá trị 0).
3. **Không chặn khi task quá hạn:** ghi lý do (magic, tên, số giây trễ, CRC) vào section `.noinit` ở SRAM4, rồi ngừng kick ngay. Lần boot sau, `init` ghi log lý do và xóa nó. Có test mã hóa và giải mã, kể cả khi magic hoặc CRC sai.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa.
