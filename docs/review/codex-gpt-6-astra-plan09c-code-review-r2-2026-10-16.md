NOT APPROVED

Còn một lỗi **Important** tại [web_check.py:243](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/bench/web_check.py:243): retry đang bao quanh **toàn bộ `req()`**, gồm gửi request, nhận header và đọc body, không chỉ connect.

Kịch bản: board đã nhận POST nhưng chậm phản hồi quá 5 giây; bench gửi lại với timeout mặc định 20 giây, rồi tính PASS và ghi nhầm là “connect blip”. Điều này che lỗi xử lý HTTP.

**Sửa:** chỉ retry khi `connect()` timeout trước khi gửi request. Timeout sau đó phải làm case thất bại; đóng kết nối trong `finally`. Thêm kiểm nhỏ phân biệt hai loại timeout, không cần board stub.

Hai sửa về giải phóng JSON và reset phiên streaming đã đạt. Chấp nhận ruling không dựng bộ stub lớn chỉ để test helper `push()`.