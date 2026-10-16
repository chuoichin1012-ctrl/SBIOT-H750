Plan 08c, code review r3: commit 93c7bc1 sửa điểm của r2.

- Accumulator Edge của client giờ được gắn khi client gửi dữ liệu lần đầu, trong task bridge, ở `from_network` (`if (cl && !cl->eacc) eacc_attach`). Lúc accept/connect không còn gắn.
- Vì vậy client kết nối trước hay sau `bridge_set_edge` đều đi chung một đường code. Mọi ca bench có ghép request đều chạy qua đường này: `split`, `gwrtu`, `partial`, `contention`, `bigreport`. Tất cả đã chạy lại trên firmware mới và đạt.
- Gắn và gỡ accumulator chỉ diễn ra trong task bridge (gỡ ở drop/abort).

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi.
