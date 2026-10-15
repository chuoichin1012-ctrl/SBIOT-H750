QUYẾT ĐỊNH: NOT APPROVED

Không thấy lỗi Critical; còn **hai lỗi Important**:

1. **SPI4 khởi tạo lỗi nhưng vẫn được sử dụng** — [boot.c:162](/home/linh/projects/SBIOT-Gateway/stm32/firmware/boot/boot.c:162), [spi_nor.c:103](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/board/spi_nor.c:103). Lỗi chỉ được log; callback đọc/xóa/ghi vẫn hoạt động. Nếu đọc JEDEC lỗi một lần nhưng các lệnh sau thành công, bootloader vẫn cài staging hoặc gieo factory, trái ruling 3. **Sửa:** khóa I/O sau init thất bại; QSPI hợp lệ vẫn được chạy. Test phải giả lập init lỗi nhưng bus sau đó vẫn hoạt động.

2. **`WIP=0` chưa chứng minh sector đã xóa** — [spi_nor.c:73](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/board/spi_nor.c:73). GD25Q32E bỏ qua lệnh xóa sector bị bảo vệ. Driver vẫn trả OK, nên Reload có thể phục hồi factory nhưng giữ nguyên staging; boot tiếp theo lại cài staging. **Sửa:** kiểm đọc lại việc vô hiệu hóa staging trước khi sửa QSPI, hoặc bảo đảm `erase_sector()` kiểm chứng kết quả. Thêm ca sector bị bảo vệ. [Datasheet §7.15, trang 27](https://www.mouser.com/datasheet/2/870/gd25q32e_rev1_3_20221013-1825518.pdf).

**Bảy ruling:** 1, 2, 4, 7 chấp nhận; 3 chưa được mã thực hiện đúng; 5 chấp nhận với giới hạn **R20 chưa lắp**; 6 chấp nhận cách giả lập nhưng chưa thay thế kiểm SW1 thật.

**Các ca board còn thiếu đáng chú ý:**

- SW1 thật, gồm khôi phục từ vòng `INSTALL_HALT`, không chỉ giữ nút lúc boot.
- Hai đường lỗi trên; upload bị ngắt rồi boot lại.
- Đọc lại toàn slot sau ca chống clone: [ota_check.py:133](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/bench/ota_check.py:133) hiện chỉ kiểm log “erased” và app không chạy, chưa chứng minh xóa vật lý. Cũng cần kiểm riêng nhánh app từ chối binding.

Chưa thấy lỗi watchdog hoặc chuyển QSPI indirect → memory-mapped khác có căn cứ. `check_pins` chạy đạt; tôi không chạy lại bench hay điều khiển board.