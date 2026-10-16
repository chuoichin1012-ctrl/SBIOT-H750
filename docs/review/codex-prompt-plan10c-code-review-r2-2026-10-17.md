Plan 10c, review code vòng 2. Đã sửa cả hai lỗi Important của vòng 1 (commit d6ac133):

1. **`serial_transact`:**
   - một deadline `until` dùng chung cho vòng chờ `tail` và vòng chờ khung đang tới;
   - ở mỗi vòng lặp, kiểm hết hạn trước rồi mới chấp nhận rằng bus đã yên;
   - mỗi lần ngủ không vượt quá `min(gap + 1, thời gian còn lại)`, và `left_ms` luôn ≥ 1.
2. **Placeholder:** `js_parse_root` (theo RFC 8259) quyết định placeholder có phải literal hay không. Chỉ số, true, false và null là literal; mọi thứ khác được quote thành chuỗi. Test: `01` và `1.` thành chuỗi, `-0.5e3` giữ là số.

`test_proto`, `test_edge_core`, `test_edge_text` xanh, firmware build được. Phạm vi: `git diff d6ac133~1 d6ac133 -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important.
