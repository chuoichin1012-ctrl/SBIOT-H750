Plan 09b r4 (`stm32/docs/plans/2026-10-16-09b-config.md`) sửa đủ các điểm của r3.

1. Các chỗ mâu thuẫn:
   - Review Focus 5 giờ chỉ nói topics/events, còn Edge giữ quy tắc hiện tại;
   - ruling 3 bỏ câu "tắt Edge";
   - ruling 2 ghi khóa FAL áp dụng cho cả lỗi đọc lẫn lỗi ghi;
   - Global Constraints đổi thành "không thêm cơ chế mới", vẫn giữ lỗi khởi tạo và các trạng thái Edge sẵn có.
2. Ruling 4 có hợp đồng an toàn bộ nhớ:
   - số hữu hạn, số nguyên, nằm trong giới hạn trước khi ép kiểu;
   - độ dài chuỗi tính bằng byte, chừa chỗ cho NUL;
   - phần tử danh sách không phải object thì bỏ qua;
   - hết bộ nhớ thì trả NULL. Có Review Focus 6 và test quét lỗi cấp phát (RED: 280/283 lần nạp ra config thiếu key; nay GREEN).

T3 đã xong, commit mới nhất: `cfg_load` nạp từng trường (`git diff ada2033 HEAD -- stm32/firmware`). Trên board: lưu port2 với `listenPort` 70000 thì vẫn lưu được, GET trả đúng từng byte; sau khởi động lại log ghi "defaults used for listenPort" và port2 vẫn chạy.

Trả lời APPROVED, hoặc NOT APPROVED kèm danh sách cần sửa.
