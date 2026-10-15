NOT APPROVED

Còn 2 điểm cần sửa:

1. **Important — trạng thái `declared` còn sót từ request trước:** [web.c:409](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/web.c:409). Trên cùng kết nối: POST hợp lệ có body, sau đó GET thiếu/sai Authorization. `on_headers()` thoát trước khi cập nhật `declared`; kiểm ở dòng 429 thấy độ dài khác và bỏ request, không trả 401, khiến kết nối bị treo chờ phản hồi. **Sửa:** đặt `declared` cho từng request trước các nhánh thoát sớm; thêm test POST → GET sai auth trên cùng socket.

2. **Important — test timeout handshake có thể đạt giả:** [web_check.py:399](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/bench/web_check.py:399). `socket.timeout` đang được tính là socket đã đóng, dù nó có thể vẫn mở. Sau đó test tự đóng các socket trước khi thử kết nối mới, nên cũng không chứng minh board đã thu hồi chúng. **Sửa:** chỉ EOF/reset mới tính là đóng; timeout phải làm ca kiểm thất bại. Thử kết nối mới trước khi tự đóng các socket đang giữ.

Các sửa đổi còn lại xử lý đúng những lỗi vòng trước theo đối chiếu mã. Tôi chưa chạy lại bench trên board.