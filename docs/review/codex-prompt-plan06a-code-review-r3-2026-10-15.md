Vòng 3 review code 06a: `git diff 89ffa93..HEAD -- stm32/`.

Đã sửa lỗi của vòng 2: `seed_recover` xóa SEIS, đọc bỏ 12 lần `DR` mà không chờ DRDY, rồi kiểm SEIS/SECS. Chỉ word được giữ lại mới chờ DRDY.

Test `test_rng`:
- thêm trường hợp giữ DRDY = 0 cho tới khi đủ 12 lần đọc bỏ. Trường hợp này RED với code cũ (0 lần đọc), GREEN với code mới (13 lần đọc, word thứ 13 được lấy);
- sửa luôn fake: xóa SEIS bây giờ xóa cả trạng thái cuối được lặp lại.

Trên board: boot bình thường, ping đạt, không có log "RNG FAILED".

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
