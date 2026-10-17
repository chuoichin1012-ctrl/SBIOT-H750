Plan 11a, vòng 3. Đã sửa cả hai điểm của vòng 2:

1. **Chuyển đổi (Task 6), thứ tự mới:**
   1. Khi firmware cũ còn chạy: lệnh `Z` `WIPE` (đã có sẵn) xóa toàn bộ SPI4, rồi reset. Bootloader cũ sẽ seed factory bằng app cũ; bước 4 thay lại.
   2. Nạp boot và app mới qua SWD. Bootloader mới dừng vì bản ghi v1, trước `install_run`, nên không cài gì.
   3. `flash.sh bind`, rồi boot. Staging lúc này đã trống nên app mới chạy; FlashDB được format mới.
   4. Lệnh mới `R` `RESEED` xóa sector đầu của slot factory, rồi reset. Lần boot đó seed factory từ app mới.
   5. Nếu board đã từng được hiệu chuẩn thì chạy lại `factory_cal.py`.

   Mất config và hiệu chuẩn của board phát triển là chấp nhận được. Có test chuyển đổi trên chính board bench, với staging và factory đều chứa image cũ hợp lệ.
2. **Lệnh console:** dùng `C` để đọc lại factory record, và `R` + `RESEED` để xóa magic của slot factory. Cả hai chữ đều đang trống, nên không đụng tới `F` (peer serial) và `X` (XOFF).

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa.
