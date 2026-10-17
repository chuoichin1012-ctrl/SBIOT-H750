Plan 11a, review code vòng 3. Đã sửa lỗi Important của vòng 2 (commit 8fea340):
- timeout của `mqtt_io` nâng lên 400 s, đủ cho trọn một giao dịch ghi point Edge: 60 s chờ bus cộng 65,5 s chờ phản hồi;
- vẫn beat giữa các point (`write_point`), và không beat khi chờ lock;
- spec §11.4 đã cập nhật danh sách timeout.

Đây là một hằng số nên không có test host riêng. Đã ghi ruling.

**Thêm (chỉ ở bench):** `ota_check` chờ console của app trả lời rồi mới upload, vì app boot mất vài giây. Đây là nguyên nhân thật của các lần upload qua UART7 bị hỏng.

Phạm vi: `git diff 8fea340~1 HEAD -- stm32`. Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important.
