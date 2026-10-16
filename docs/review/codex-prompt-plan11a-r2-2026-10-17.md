Plan 11a, vòng 2. Đã sửa cả 5 điểm của vòng 1 trong `stm32/docs/plans/2026-10-17-11a-identity-and-gates.md`:

1. **Task 0:**
   - task `wdog` chạy mỗi 50 ms; test với đúng nhịp đó, và với nhịp không đều của bootloader;
   - hai khoảng hở được đo riêng: `iwdgMaxGapMs` (vòng task, tiêu chí < 400 ms) và `wdiMaxGapMs` (các lần đảo WDI, tiêu chí < 600 ms).
2. **Task 2:**
   - `flock` trên `COUNTER.lock` trong suốt chu trình đọc–tăng–ghi;
   - ghi file tạm, fsync, `os.replace`, fsync thư mục, rồi mới in SN; lỗi thì không in SN;
   - test: 8 tiến trình chạy cùng lúc, và lỗi khi ghi.
3. **Task 1:** chữ ký phủ UID, MAC và SN lưu trong record (byte 4–33), và UID trong record phải bằng UID của chip; khác UID thì là `BIND_CLONE`. Test đủ các ca.
4. **Task 4:** thêm lệnh console `F` đọc lại cả hai bản factory record từ SPI4; `factory_cal.py` so giá trị, reset, rồi kiểm `aiCalibrated` sau khi boot.
5. **Task 6:** quy trình chuyển đổi một lần, không thêm code vào bootloader:
   - lệnh `X` rồi `OLD` xóa sector đầu của staging và của slot factory;
   - lần boot sau, bootloader seed factory từ app v2;
   - test: boot thường, và khôi phục bằng Reload.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa.
