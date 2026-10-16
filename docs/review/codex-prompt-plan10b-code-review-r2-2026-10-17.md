Plan 10b, review code vòng 2. Cả hai lỗi Important của vòng 1 đã sửa (commit ddc33a6):

1. **PPP thẳng đếm bước sớm:**
   - `plain_ppp` xóa `EV_PPP_DOWN` còn sót lại của phiên trước (`ppp_stop` trong `recover`) trước khi `ppp_begin`;
   - mỗi lần thức, vòng chờ hỏi `lte_plain_wait()`, một hàm thuần trong `lte_power.c`. Trước khi có địa chỉ, chỉ deadline mới kết thúc lượt chờ (NO_ADDR, một bước backoff). Một DOWN sớm, dù là DOWN cũ hay do PPP bị từ chối, vẫn đợi tới deadline;
   - test: `test_plain_wait` trong `test_lte_power`, gồm DOWN sớm, deadline, và UP→DOWN giữa hai lần thức.
2. **Có địa chỉ mà không được ghi nhận:**
   - `ppp_status` đặt `s_got_addr` khi có địa chỉ;
   - task `lte` lấy cờ này ra bằng `took_addr()` (đọc rồi xóa trong vùng găng): qua CMUX ở mỗi lượt kiểm, trên UART thường ở mỗi lần thức. Có cờ thì `lte_backoff_ok` và `recoveries = 0`, kể cả khi PPP đã rớt trước lúc task kiểm.

`test_lte_power` xanh, app build được. Phạm vi: `git diff ddc33a6~1 ddc33a6 -- stm32/firmware`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important.
