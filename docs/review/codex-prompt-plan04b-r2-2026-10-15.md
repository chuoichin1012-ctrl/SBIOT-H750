Tiếp tục trong cùng phiên. Đã sửa theo review 04b trong commit `e59908a`, xem `git diff 447c601..e59908a -- stm32/` và mục "Codex round 1" trong plan doc.

1. **SPI4 khởi tạo lỗi thì không bao giờ được dùng.** `boot.c` đưa cho `install_run` một `nor_t` mà mọi thao tác đều trả lỗi.
   - Trên board: ép `spi_nor_init` trả về `NOR_EIO` qua debugger (gán r0 ở địa chỉ trả về), trong khi bus SPI4 vẫn chạy tốt và staging đang giữ image mới. Kết quả: log ghi `FAILED`, không cài gì, app chạy. Lần boot thường sau đó thì cài.
2. **Đường Reload đọc lại magic của staging sau khi xóa**, phải thấy đã trắng thì mới đụng tới QSPI, không thì dừng với `WHY_STAGING`.
   - nor_sim có thêm `SIM_ERASE_SILENT`; test mới RED trước khi sửa, rồi GREEN.
   - Thêm mutant "staging erase not verified": mutants 25/25.
3. **Thêm ca kiểm trên board** (`ota_check.py`, giờ đạt 30/30):
   - upload bị ngắt sau 8 KB;
   - SPI4 khởi tạo lỗi;
   - Reload từ vòng dừng;
   - đọc QSPI qua SWD để xác nhận slot bị xóa sau ca clone;
   - app build với khóa khác thì từ chối bản ghi.

   Ca reset giữa lúc cài giờ tính thời điểm từ chính các lần reset của bench, và đòi ít nhất 3 lần cài bị cắt ngang. Lần chạy vừa rồi: 5 lần bị cắt, rồi một lần cài xong.
4. **Nút SW1 thật:** sẽ nhờ người dùng kiểm (ruling 6).

`boot_check.py`, cả bản thường lẫn `--hostile`, đều đạt; host suite đạt.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt: còn lỗi Critical hoặc Important nào không? Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
