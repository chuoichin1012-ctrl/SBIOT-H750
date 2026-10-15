NOT APPROVED

Còn **5 lỗi Important**:

1. **POST thiếu body vẫn có thể ghi cấu hình** — [web.c:413](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/web.c:413). Khi client gửi JSON hợp lệ nhưng ngắn hơn `Content-Length` rồi đóng kết nối, Mongoose vẫn phát `HTTP_MSG` với độ dài thực nhận. `on_message()` chuyển thẳng sang API. Cần kiểm đủ body trước mọi tác dụng phụ; thêm test FIN sớm và cấu hình không đổi.

2. **Upload sai khi có HTTP pipelining** — [web.c:366](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/web.c:366), tương tự dòng 317. GET rồi POST upload trong cùng buffer khiến header của request sau không nằm tại offset 0. Xóa chỉ `hm->head.len` làm phần header còn lại bị coi là body. Cần xóa tới vị trí bắt đầu body; test GET + POST cùng buffer.

3. **Hàng đợi gửi vẫn có thể làm cạn heap** — [api.c:31](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/api.c:31), [console.c:83](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/console.c:83). Client liên tục gửi request/lệnh nhưng không đọc response khiến API thường và phản hồi WS tiếp tục nối vào `c->send`. Cần áp dụng giới hạn cho mọi đường trả lời, kể cả control frame WS; thêm test client vừa gửi liên tục vừa không đọc.

4. **Giới hạn console bỏ sót kết nối chưa upgrade** — [console.c:86](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/console.c:86). Nhiều socket mở nhưng không gửi đủ handshake không bị tính vào giới hạn 4, có thể chiếm hết tài nguyên socket chung. Cần giới hạn cả kết nối đang chờ và đặt timeout handshake.

5. **GET Edge trả 200 dù ghim thất bại** — [web.c:428](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/web.c:428). Khi Edge không dùng được hoặc hết chỗ ghim generation, code vẫn trả 200 với body rỗng. Cần ghim và kiểm kết quả **trước** khi gửi header; trả lỗi thích hợp, gồm 503 khi hết chỗ ghim.

**B4:** chấp nhận giới hạn đã biết trong phạm vi hiện tại; không bắt buộc thêm kiểm layout lúc mount. Board từng dùng layout khác phải factory reset trước khi sử dụng.

Các binary host hiện có `test_clock`, `test_web_core`, `test_arena` chạy đạt. Những lỗi trên được xác nhận bằng luồng mã; tôi chưa tái hiện chúng trên board.