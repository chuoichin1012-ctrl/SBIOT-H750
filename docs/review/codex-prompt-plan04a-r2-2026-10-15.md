Vòng 2 cho Plan 04a (cùng file, branch `plan04a-ota-host`). Mọi phát hiện của vòng 1 đã sửa trong prototype, rồi sinh lại plan từ mã đã kiểm. Bảng "Codex round 1, and how each finding is handled" ghi lại từng sửa đổi:

1. **Cứu hộ có thử lại (D4):**
   - không còn nguồn nhưng QSPI hợp lệ thì về CONFIRMED và chạy QSPI;
   - lỗi đọc hoặc ghi bản ghi đi qua `rec_failed()`, tức hàng 3–6: QSPI hợp lệ thì chạy degraded, không xóa, và `rec_fail` được đếm.
2. **Timeout khi đọc giữ nguyên là timeout**, nên thành hàng 2. Đã thêm fault đọc (`SIM_READ_EIO`, `SIM_READ_TIMEOUT`) vào simulator, có test ở cả đường checkpoint và đường RAM.
3. **`bootrec_read` có ba kết quả** (D10): hợp lệ, không có, lỗi I/O.
   - `bootrec_write` không ghi khi không xác định được bản mới nhất;
   - bootloader coi lỗi đọc bản ghi là hàng 3–6;
   - app từ chối OTA, confirm và rollback.
   
   Đã thêm test lỗi đọc thoáng qua một lần trong TRIAL: TRIAL phải được giữ.
4. **`setjmp`:** bench của `one_run` chuyển lên heap.
5. **Các test còn thiếu:**
   - mất điện liên tiếp trong RESTORE (8 lần);
   - SHA đích sai sau khi chép bằng đường RAM;
   - handoff có magic đúng nhưng CRC sai;
   - D7 (bản ghi đã ghi xong nhưng đọc lại lỗi: trial vẫn được đếm);
   - thêm 300 lượt cắt điện 3 lần cho mỗi ma trận.

Kết quả: `test_install` đạt, cả hai ma trận cộng các lượt cắt 3 lần (2 phút 12 giây dưới ASan). Mutant: **20/20** bị bắt, gồm 4 mutant mới khôi phục đúng các lỗi bạn chỉ ra.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt: còn lỗi chặn nào không? Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`, dòng thứ hai là `THỰC THI: <SUBAGENT | NATIVE>`.
