NOT APPROVED

Task 0b cần chốt thêm 3 điểm:

1. **Phân biệt chờ hợp lệ với bị kẹt.** Câu “mọi wait vô hạn đều thêm heartbeat” có thể che deadlock, chẳng hạn vòng `mqttgw.c::lock_wait()`. Ngược lại, chỉ beat ở vòng ngoài sẽ reset nhầm khi MQTT đang chờ `retryInterval=300 s`, Edge chờ kỳ report dài, hoặc serial/syslog không có dữ liệu.  
   Ghi rõ: chờ dữ liệu và ngủ theo lịch được chia nhỏ để beat, nhưng giữ nguyên deadline nghiệp vụ; chờ mutex bị kẹt không được tự nuôi heartbeat mãi. Thêm test cho một lần chờ hợp lệ dài hơn timeout và một trường hợp khóa không tiến triển.

2. **Hợp đồng đồng thời của `hb_add` chưa đủ.** Một lần ghi timestamp 32 bit không bảo vệ việc cấp slot và công bố tên/timeout. Các task có thể đăng ký khi scheduler đã chạy, trong lúc `wdog` quét bảng.  
   Chốt cách đơn giản: đăng ký trong critical section, công bố slot sau khi khởi tạo đầy đủ; timestamp dùng atomic; tên phải được sao chép hoặc có vòng đời tĩnh. Không được âm thầm bỏ giám sát khi hết slot.

3. **Không thể bảo đảm lưu log chỉ bằng chờ 1 s.** `blog()` hiện lấy mutex với `portMAX_DELAY`; syslog có thể chính là task bị kẹt, queue đầy, hoặc hết ngân sách ghi flash.  
   Đường xử lý lỗi của watchdog phải không chặn. Trong thời gian chờ 1 s vẫn duy trì cả hai watchdog, rồi mới ngừng kick. Nếu bắt buộc biết tên task sau reboot, lưu nguyên nhân tối thiểu vào backup register/SRAM rồi ghi log sau boot; nếu không, phải ghi rõ log trước reset chỉ là *best-effort* và sửa tiêu chí nghiệm thu tương ứng.

Không cần thêm cơ chế phục hồi ngoài reset.