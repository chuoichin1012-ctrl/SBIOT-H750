NOT APPROVED

Còn **1 lỗi Important** tại [rng_core.c:16](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/board/rng_core.c:16): đường phục hồi seed error chờ `DRDY` trước mỗi lần bỏ word. Khi RNG đã ngừng sinh dữ liệu, `DRDY` có thể bằng 0, khiến hàm timeout trước khi hoàn tất việc làm sạch pipeline.

**Cần sửa:** sau khi xóa `SEIS`, đọc bỏ **12 lần `DR` không chờ `DRDY`**, rồi kiểm trạng thái lỗi theo [RM0433 §34.3.7](https://www.st.com/resource/en/reference_manual/dm00314099-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf). Chỉ chờ `DRDY` khi lấy word mới để sử dụng. Thêm test giữ `DRDY=0` cho tới khi đủ 12 lần đọc bỏ; fake hiện tại luôn cho `DRDY=1` nên che lỗi này.

Phần đổi tốc độ/duplex đã đạt. Hai binary host có sẵn `test_rng`, `test_ip101` chạy lại đều đạt; tôi không rebuild hoặc chạy board.