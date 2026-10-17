Plan 11a, review code vòng 2. Đã sửa cả 4 lỗi Important của vòng 1:

1. **Phần nối Task 0b chưa được commit:** đã commit (2f15c2e). HEAD build sạch và `check_elf` đạt.
2. **Bridge ở trạng thái ERROR:** vòng ngủ có beat mỗi 10 s; task không treo, chỉ đứng yên.
3. **`edge_rpt` chờ một report HTTP dài:**
   - slot ghi lại task chủ của nó (`hb_add_owned`, `hb_find`);
   - `wdog_hb_beat_self()` beat cho chính task đang gọi;
   - `bridge_send` gọi nó ở mỗi vòng chờ 10 s. Bridge thực hiện report cũng đang được giám sát, nên chờ ở đây không che một lần treo nào.
4. **Ghi nhiều point Edge qua MQTT hoặc bridge:** `write_point()` gọi `wdog_hb_beat_self()` mỗi khi ghi một point. Không beat khi đang chờ lock.

**Thêm:** upload qua UART7 trong `ota_check` từng hỏng khi trùng lần ghi log đầu tiên sau boot, vì FlashDB chiếm SPI4 hơn 5 s. Đã sửa: upload qua UART7 chờ mỗi khối 30 s, và lệnh console `I` phải xác nhận bằng dòng `HANG`. Web OTA không bị ảnh hưởng.

Test host `test_board` (có thêm ca owner) đạt, firmware build được. Phạm vi: `git diff 2f15c2e~1 HEAD -- stm32` (bao gồm cả phần nối đã commit).

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important.
