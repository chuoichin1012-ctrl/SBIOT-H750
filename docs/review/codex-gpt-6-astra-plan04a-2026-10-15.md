QUYẾT ĐỊNH: NOT APPROVED
THỰC THI: NATIVE

Tách 04a/04b hợp lý, nhưng còn các lỗi cần sửa trong [Plan 04a](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-04a-ota-host.md):

1. **Cứu hộ bỏ qua bảo vệ QSPI — [dòng 2632](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-04a-ota-host.md:2632).** `install_rescue_retry()`:
   - Dừng hẳn khi backup/staging hỏng, dù QSPI còn hợp lệ.
   - Khi `put()` lỗi, gọi thẳng `ram_path()`, có thể xóa QSPI hợp lệ và không tăng `rec_fail`.
   
   Trái §11.2 hàng 1, 3–4. Phải xét `Vq` và xử lý lỗi record trước khi vào đường RAM; thêm test cả hai tình huống.

2. **Timeout đọc bị mất — [dòng 2356](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-04a-ota-host.md:2356).** Cả đọc nguồn và đọc lại đích đều đổi `NOR_TIMEOUT` thành `NOR_EIO`, dẫn tới REBOOT thay vì WATCHDOG theo hàng 2. Giữ nguyên mã lỗi; thêm fault injection cho đọc ở cả đường checkpoint và RAM.

3. **Lỗi đọc record bị coi là record không hợp lệ — [dòng 2054](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-04a-ota-host.md:2054).** Ví dụ A mới nhất nhưng tạm thời không đọc được: `bootrec_write()` có thể chọn xóa chính A. `ota_stage_begin()` cũng có thể cho phép upload khi không đọc được record TRIAL. Cần phân biệt **không có record hợp lệ** với **lỗi I/O**, không xóa/cho phép OTA khi chưa xác định được trạng thái.

4. **Ma trận cắt điện có trạng thái không xác định — [dòng 1905](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-04a-ota-host.md:1905).** `bench_t b` là biến automatic không `volatile`, bị sửa giữa `setjmp` và `longjmp`, rồi tiếp tục được sử dụng. Theo [C11 §7.13.2.1¶3](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf), giá trị đó trở thành indeterminate. Chuyển trạng thái bench sang heap hoặc đặt ngoài hàm chứa `setjmp`, rồi chạy lại ma trận.

**Test §12.1 còn thiếu:** mất điện **liên tiếp** trong RESTORE; SHA đích sai sau chép bằng đường RAM; handoff có **magic đúng nhưng CRC sai**. Hai ma trận hiện mỗi lượt chỉ cắt một lần; ca handoff hiện sai cả magic nên chưa chứng minh kiểm CRC.

**D1–D9:** chấp nhận D1, D3, D5, D6, D9 về nguyên tắc. D2 chỉ áp dụng REBOOT cho lỗi không phải timeout. D4 cần sửa mục 1; D8 cần sửa mục 3. D7 phải bỏ khẳng định “trial không được đếm”: nếu CRC đã ghi thành công nhưng đọc lại lỗi, record mới vẫn có thể hợp lệ; cần test trường hợp này.

Đây là kết luận từ đọc mã trong plan; tôi chưa chạy lại prototype hay các ma trận.