Plan 09b r3 (`stm32/docs/plans/2026-10-16-09b-config.md`): người dùng vừa đổi hướng (2026-10-16), nguyên văn:

> "tôi không yêu cầu thiết bị phải tự xử lý tất cả các lỗi, hãy giữ mọi thứ thật đơn giản, nếu người dùng cấu hình sai, đơn giản là thiết bị chạy với cấu hình đó, có thể nó không hoạt động, không kết nối được, vấn đề xử lý lỗi do người dùng phụ trách, thiết bị chỉ cần hoạt động theo các cấu hình được cung cấp"

Đây là quyết định của người dùng, nên không cần duyệt lại. Plan đã sửa theo đó như sau:
- T1 và T2 đã xong (commit 1f20581 và ada2033). Trong T2 có khóa FAL sau một lần ghi lỗi, đúng theo lựa chọn của bạn.
- Các ruling 5–7 và 10 của r2 bị rút: tắt dịch vụ, safe mode, thứ tự listen port, trạng thái cho DB không mount được.
- Ruling 4 mới: bộ nạp đi **từng trường**. Trường sai kiểu, vượt giới hạn hoặc chuỗi quá dài thì lấy giá trị mặc định của trường đó, ghi log; các trường còn lại dùng nguyên như đã lưu. Không kiểm quy tắc chéo. Mô hình Edge nạp như hiện nay (pha 1–2).
- T4 bỏ test chéo, vì phía C không còn phán config đúng hay sai.
- Không thêm các ma trận lỗi kéo dài hay ghi dở mà bạn đề xuất ở câu FlashDB, vì "giữ đơn giản"; ghi thành giới hạn đã biết.

Xin kiểm: plan r3 có nhất quán và đủ để thực hiện không, và ruling 4 có giữ an toàn bộ nhớ không. Trả lời APPROVED, hoặc NOT APPROVED kèm danh sách cần sửa, và nhớ giữ đơn giản.
