Review lại các sửa đổi sau review cuối của Plan 04a, commit `be6dec8` trên branch `plan04a-ota-host` (`git diff c3b0e2a..be6dec8`):

1. **Important, lỗi đọc image:** `IMG_EREAD` không còn bị cache thành "hỏng".
   - `check()` đặt cờ `read_error`.
   - `unsure()` reset rồi quyết định lại, tối đa 3 boot liên tiếp, đếm bằng `handoff.read_fail` (lấy từ byte pad, block vẫn 64 B). `read_fail` về 0 khi nhảy vào app.
   - Sau 3 lần: không có gì hợp lệ mà còn image không đọc được thì vào cứu hộ có thử lại, không dừng hẳn (`nothing_valid()`).
   - `rec_failed` không vào đường RAM khi QSPI chưa đọc được (`unsure`).
   - Test `test_image_read_errors`: QSPI đọc lỗi một lần, backup đọc lỗi một lần, backup không đọc được trong một thời gian. Cả ba đều RED trước khi sửa (kết quả cũ là halt), GREEN sau khi sửa.
2. **Minor, hàng 1 xét trước:** `rec_failed()` xét hàng 1 trước mọi nhánh phục hồi. Test: bản ghi không đọc được và không có gì hợp lệ thì dừng ngay ở boot đầu (trước đây mất 3 boot).
3. **Minor, nhóm cắt nhiều lần:** đếm số lần cắt thực tế, đổi nhãn thành "up to three", và assert ít nhất 100/300 lượt có đủ 3 lần cắt. Thực tế: cài đặt 174/300, rollback 220/300.

Kết quả: `make -C stm32/firmware/test/host` đạt cả 10 bộ. Mutant 22/22 bị bắt, gồm 2 mutant mới: "read error cached as a bad image" và "row 1 not first on a record failure".

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt. Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`. Liệt kê lỗi còn lại, nếu có.
