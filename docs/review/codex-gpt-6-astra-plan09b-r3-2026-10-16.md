NOT APPROVED

Hướng đơn giản hóa được chấp nhận. Cần sửa hai điểm trong plan:

1. **Dọn các quy định còn mâu thuẫn.**
   - Review Focus 5 nói cắt cả danh sách thiết bị/điểm; ruling 4 lại giữ `cfg_edge_load` hiện tại, vốn có thể từ chối mô hình vượt giới hạn. Chốt cắt **topics/events**, còn Edge giữ hành vi hiện tại như bạn đề xuất.
   - Ruling 3 vẫn nói `edgeReport` lỗi thì tắt Edge theo ruling 5 cũ; bỏ câu này.
   - Ruling 2 còn tham chiếu “ruling 10”; cập nhật và ghi khóa FAL áp dụng cả lỗi đọc **lẫn ghi**.
   - Câu “không dịch vụ nào bị tắt, không error states” cần giới hạn thành **không thêm cơ chế mới**; vẫn giữ lỗi khởi tạo và trạng thái Edge hiện có.

2. **Ruling 4 cần chốt thêm hợp đồng an toàn bộ nhớ trong T3.**
   - Giữ kiểm số **hữu hạn**, số nguyên cho trường integer và giới hạn trước khi ép kiểu; chiều dài chuỗi tính bằng byte, chừa NUL.
   - Quy định phần tử danh sách không phải object, như `[null, 12, {}]`. Đề xuất bỏ phần tử sai cấu trúc, log; object thiếu/sai trường thì điền default từng trường.
   - **Hết bộ nhớ không phải trường cấu hình sai.** Nếu tạo/copy default thất bại, giải phóng kết quả dở và trả NULL theo đường lỗi hiện có; không trả một object thiếu trường cho runtime. Thêm test cấp phát thất bại khi dựng default, không cần ma trận lỗi phần cứng mới.

Với các điều kiện này, nạp từng trường là phương án hợp lý để giữ an toàn bộ nhớ. Không yêu cầu khôi phục safe mode, kiểm quy tắc chéo hay các ma trận FlashDB đã được người dùng loại khỏi phạm vi.