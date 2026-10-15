QUYẾT ĐỊNH: NOT APPROVED

Còn **một lỗi Important** tại [install.c:173](/home/linh/projects/SBIOT-Gateway/stm32/firmware/boot/install.c:173): `ram_path()` gọi trực tiếp `img_check()` và bỏ qua `IMG_EREAD`, không đặt `read_error`.

**Kịch bản:** QSPI/staging hỏng, backup hợp lệ; `rec_failed()` kiểm backup thành công rồi vào đường RAM khi `rec_fail ≥ 3`. Lần kiểm lại backup trong RAM gặp lỗi đọc thoáng qua → bỏ qua nguồn; `nothing_valid()` thấy `read_error == false` và trả `INSTALL_RESCUE_HALT`, dù backup vẫn hợp lệ.

**Cần sửa:** truyền trạng thái chưa đọc được vào cơ chế reset/thử lại tại nhánh này. Thêm test lỗi đọc ở **lần kiểm backup thứ hai**, xác nhận không HALT và phục hồi được khi lỗi hết.

Hai sửa đổi Minor đã đạt. `test_install` chế độ quick đạt trên binary sẵn có; tôi chưa chạy lại toàn bộ ma trận.