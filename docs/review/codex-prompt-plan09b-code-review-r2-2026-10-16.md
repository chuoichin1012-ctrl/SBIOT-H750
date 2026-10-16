Plan 09b code review r2. Commit mới nhất của `plan09b-config` sửa cả 4 lỗi của r1:

1. `json_alloc_install()` được gọi ở `app_main.c` trước `store_init`, tức trước khi parse bất kỳ cấu hình nào.
2. Giới hạn văn bản trên UI tính bằng byte UTF-8 (`fieldError`, `topicError`). Có test: 9 lần `é` (18 byte) giờ bị từ chối.
3. Trang Port và System chỉ cho Save khi đã đọc xong các cấu hình dùng cho kiểm chéo; đọc lỗi thì báo, yêu cầu tải lại trang. Board chỉ có 1 UART thì không đọc port2.
4. Import CSV chạy đúng các kiểm tra của hộp thoại sửa thiết bị và điểm (`modelErrors`, gọi `slaveDefs`/`nodeDefs` + `formErrors`) cho mọi thiết bị và mọi điểm. Kiểm trên board: CSV có reg 70000 bị dừng với "plc/t: reg: Enter 0–65534."

Kiểm thử: `npm run test:unit` 8/8; `test:pages` đạt trên board; app build và `check_elf` đạt.

Phạm vi: `git diff HEAD~1 HEAD -- stm32`. Trả lời APPROVED hoặc NOT APPROVED kèm lỗi Critical/Important.
