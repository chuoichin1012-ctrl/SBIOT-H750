NOT APPROVED

Bốn lỗi cũ đã được xử lý, nhưng còn **một lỗi Important mới**: task `reload` vừa lấy mẫu nút mỗi 50 ms vừa ghi FlashDB có thể chặn 430 ms.

Ví dụ: ghi flash từ giây 2,70 đến 3,13; người dùng nhả nút ở giây 2,80. Khi task chạy lại, `reload_step(..., now_ms)` có thể tính thành giữ 3,13 giây và **factory reset ngoài ý muốn**.

Cần giữ việc lấy mẫu nút độc lập với ghi flash. Cách đơn giản: lấy mẫu trong task `io`, gửi yêu cầu reset sang `reload`. Thêm test nhả nút ngay dưới 3 giây trong lúc lưu chậm, bảo đảm không reset.