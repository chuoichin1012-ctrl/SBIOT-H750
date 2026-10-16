Plan 09b code review r3. Commit mới nhất sửa đủ 3 lỗi của r2:

1. `fieldError` đếm byte trên chuỗi đúng như sẽ gửi đi, không `trim()`. Có test: 8 lần `é` cộng một dấu cách (17 byte) bị từ chối.
2. Trang Port chỉ cho Save khi lần đọc của chính UART đang mở đã xong và không lỗi (`checked = !loading && !error && data.n === n`). Trang System cũng vậy: phải đọc xong và không lỗi.
3. Import CSV kiểm giá trị của các trường select và radio phải nằm trong danh sách lựa chọn (`importErrors`). Trên board: fc 99 bị dừng với "fc: 99 is not one of the choices.", CSV hợp lệ vẫn import được. Chỉ kiểm lúc import, không kiểm ở mọi form, để các giá trị đã lưu sẵn (như TLS 1) vẫn lưu lại được.

Kiểm thử: `test:unit` 8/8; `test:pages` đạt trên board.

Phạm vi: `git diff HEAD~1 HEAD -- stm32/web`. Trả lời APPROVED hoặc NOT APPROVED.
