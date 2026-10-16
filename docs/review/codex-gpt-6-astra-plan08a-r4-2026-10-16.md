**APPROVED**

Bản sửa đã giải quyết điểm chặn: timeout TCP là chính sách không tiến triển, UDP giữ datagram khi lỗi tạm thời, và lần gửi thất bại vẫn nhường CPU.

Khi triển khai, cập nhật §5.6 và ngân sách stack AXI §5.3 như plan đã cam kết; chạy các host test và bench đã liệt kê.