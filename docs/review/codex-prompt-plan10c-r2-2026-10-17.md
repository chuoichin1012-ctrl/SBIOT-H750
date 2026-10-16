Plan 10c, vòng 2. Đã sửa cả 4 điểm của vòng 1 trong `stm32/docs/plans/2026-10-17-10c-review-fixes.md`:

1. **Task 1:** request `rw_prot` hợp lệ, cộng một member `z` lồng sâu D tầng. Ở đúng giới hạn (cả document sâu 16 tầng) thì vẫn có trả lời; 17 tầng và 300 tầng thì trả NULL. Với `edge_tmpl_compile`, biên cũng là 16 và 17. Trước khi sửa, test phải FAIL.
2. **Task 5:**
   - `bool (*begin)(void *ctx)`; trả false thì trả exception `MB_EX_DEVICE_FAILURE`, không đọc thanh ghi nào, không gọi `end`;
   - `end` được gọi đúng một lần, kể cả khi lỗi giữa vòng đọc;
   - sửa trong `mb_serve`;
   - test các ca: lấy khóa thất bại, lỗi ở thanh ghi thứ 3, và map không có hook.
3. **Task 6:**
   - một chủ `s_owner`, nhận và nhả bằng CAS nguyên tử;
   - `begin` nhận quyền trước khi đụng tới context hay xóa staging; `begin` thất bại sau khi đã nhận thì trả lại quyền;
   - finish OK đặt `done` và giữ quyền tới khi reboot;
   - `release` chỉ có hiệu lực với chủ chưa `done`;
   - test: B bị từ chối rồi cleanup trong khi A vẫn upload, cleanup sau khi thành công, begin lỗi khi xóa.
4. **Task 9:**
   - đồng hồ chạy khi hàng gửi chuyển từ rỗng sang có dữ liệu, chạy lại từ đầu sau mỗi lần ghi được byte, và dừng khi hàng rỗng;
   - test: phản hồi đầu tiên sau một giờ rảnh, ca đứng yên, ca vẫn tiến triển, hàng rỗng rồi đầy lại, và wrap.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa.
