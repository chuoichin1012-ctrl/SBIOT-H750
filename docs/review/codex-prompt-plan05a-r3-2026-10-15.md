Tiếp tục trong cùng phiên. Đã sửa theo review r2 trong commit mới nhất trên `plan05-storage`, xem `git diff 0c11b5c..HEAD -- stm32/` và mục "Codex round 2" trong plan doc. Mọi bản sửa đều có test RED trước.

1. **Lỗi đọc khi dọn rác.** Có ma trận đọc lỗi mới, cho sector chứa KV `keep`: một lần ở bất kỳ đâu (kể cả lúc mount), và 3 lần liên tiếp sau khi mount. Ma trận này tìm ra thêm hai lỗi của FlashDB. Các bản sửa:
   - FAL đọc lại tối đa 3 lần;
   - `move_kv` chỉ chép những gì đọc được;
   - `_fdb_flash_read` bật cờ khi đọc lỗi, và `do_gc` giữ lại sector nếu trong lúc dọn có lần đọc lỗi.
2. **Khôi phục chưa xong thì mount trả `FDB_WRITE_ERR`.** Các trường hợp: ghi trạng thái lỗi, chuyển KV `PRE_DELETE` thất bại không phải vì hết chỗ, hoặc hết số lần duyệt lại. `store` khi đó vào trạng thái "edge không dùng được" (§9.1): không phục vụ model nào kể cả mặc định, từ chối lưu, không xóa gì cho tới khi boot lại. Test kiểm cả dữ liệu lẫn kết quả trả về.
3. **Sau một lần ghi lỗi bất kỳ khi lưu, `store` mount lại.**
   - Manifest mới có mặt: đã commit.
   - Manifest cũ, hoặc không có manifest khi trước đó cũng không có: xóa các bản ghi mới.
   - Mọi trường hợp khác: trạng thái không dùng được.

   Ma trận lỗi ghi nay chạy cả từ DB trống, và còn kiểm không còn bản ghi lạc.
4. **Ruling 8:** lỗi đọc *dai dẳng* lúc mount (flash hỏng) nằm ngoài khả năng xử lý; FAL chỉ bao được lỗi thoáng qua.

Mutant "xóa bản ghi trong nhánh commit không rõ" đã bỏ khỏi danh sách vì tương đương: nhánh đó chỉ chạy khi mount lại thất bại, tức đang ghi lỗi, nên lệnh xóa cũng thất bại. Lý do có ghi trong `mutants_ota.py`. Mutant của `store`: 23/23.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt: còn lỗi Critical hoặc Important nào không? Ruling 8 có chấp nhận được không? Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
