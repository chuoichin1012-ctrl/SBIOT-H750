Tiếp tục trong cùng phiên. Đã sửa theo review r4, xem `git diff 0260686..HEAD -- stm32/`:
1. `store_state_get` trả 0 khi lần đọc đã lỗi hẳn. Test mới `test_state_read_failures` cho từng lần đọc lỗi 3 lần liên tiếp, RED trước khi sửa, rồi GREEN.
2. Edge chỉ phục vụ và lưu qua `edge_usable()`: generation đã biết, và chưa có lần đọc sai nào. Ma trận đọc lỗi lúc boot giờ kiểm: store chỉ đọc thì không phục vụ model nào, kể cả trên flash chưa có model. RED trước khi sửa, rồi GREEN.

Mutant của `store`: 26/26.

**Phạm vi chịu lỗi, người dùng đã đồng ý** (spec §9.1 và plan doc):
- phải chịu được mất điện ở bất kỳ lúc nào, và lỗi đọc thoáng qua;
- lỗi phần cứng dai dẳng thì chỉ cần không làm hỏng thêm: chuyển sang chỉ đọc, từ chối lưu, báo lỗi;
- sự cố ngoài phạm vi này ghi vào "giới hạn đã biết", không sửa thêm.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt: trong phạm vi trên, còn lỗi Critical hoặc Important nào không? Điều gì nằm ngoài phạm vi thì liệt kê thành giới hạn đã biết. Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
