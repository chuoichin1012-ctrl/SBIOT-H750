Plan 10b, review code vòng 3. Đã sửa lỗi Important của vòng 2 (commit 4817d24):
- `took_addr()` trả true thì `last_addr = ms()`, nên lượt chờ mới tính từ lúc có địa chỉ;
- `test_wait_restarts_at_address` cố định chuỗi đó trên các hàm thuần: đã chờ 8 trong 10 phút, có địa chỉ, giới hạn về 2 phút, đo từ lúc có địa chỉ. Phần nối dây là code chạy trên board.

`test_lte_power` xanh, app build được. Phạm vi: `git diff 4817d24~1 4817d24 -- stm32/firmware`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important.
