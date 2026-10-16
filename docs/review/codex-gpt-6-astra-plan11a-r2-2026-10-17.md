NOT APPROVED

Còn 2 điểm cần sửa:

1. **Task 6 — thứ tự chuyển đổi chưa chạy được khi staging còn image cũ.** `flash.sh bind` reset board; ngay lần boot đó, `install_run()` cài staging cũ lên QSPI trước khi app v2 chạy. App cũ từ chối record v2, nên không thể vào console để gửi `X OLD`.  
   Phải vô hiệu hóa staging/factory **trước lần boot được phép cài image**, bằng một quy trình cụ thể qua SWD hoặc firmware chuyển tiếp. Nếu dùng firmware chuyển tiếp, cần tránh reboot khiến factory lại được seed bằng app cũ. Thêm ca chuyển đổi với staging và factory đều chứa image v1 hợp lệ.

2. **Task 4/6 — hai lệnh console mới trùng lệnh hiện có.** Trong `app_main.c`, `F` đã dùng cho peer serial, `X<port>` dùng tiêm XOFF. Chọn lệnh khác hoặc quy định phân biệt cú pháp rõ ràng, cập nhật dispatcher và các script liên quan; không làm mất các ca bench serial hiện tại.

Các sửa về nhịp watchdog, bộ đếm SN và chữ ký UID đã giải quyết các điểm tương ứng của vòng trước.