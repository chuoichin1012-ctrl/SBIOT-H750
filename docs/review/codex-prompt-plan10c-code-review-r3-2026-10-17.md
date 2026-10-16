Plan 10c, review code vòng 3. Đã sửa lỗi Important của vòng 2 (commit b1f4ac0): `rx_quiet_since(p)` được gọi trước, `now_us()` sau, mỗi lời gọi một câu lệnh riêng; bus chỉ được coi là im lặng khi `now > since` và `now - since` ≥ `gap + 1` ms. Phần kiểm deadline giữ nguyên.

Firmware build được. Phạm vi: `git diff b1f4ac0~1 b1f4ac0 -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important.
