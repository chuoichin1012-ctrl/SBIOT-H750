Tiếp tục trong cùng phiên. Tôi đã sửa theo review r4 trong commit `f2f12e2`, xem `git diff af7d8e0..f2f12e2 -- stm32/`:

1. **`copy()` đọc lại toàn bộ đích để so với nguồn, rồi mới ghi magic.** Magic là lần ghi cuối cùng, sau đó mới đến `img_check`. Test mới:
   - `test_seed`: gieo bị ghi sai im lặng thì chỉ báo lỗi, slot factory không có magic, và lần boot sau khi gỡ lỗi thì gieo được;
   - `test_magic_program_fails`: chính lần ghi magic bị sai im lặng thì dừng.
2. **Phía upload:**
   - `ota_stage_begin` xóa ngay sector đầu của staging;
   - page 0 được ghi mà chừa trống magic;
   - `finish` chỉ ghi magic sau khi SHA khớp, rồi mới `img_check`.

   Test mới: upload dừng đúng ở ranh giới sector, đè lên staging cũ có cùng phần đuôi, thì không được cài; upload dừng ngay trong page đầu cũng làm image cũ hết hiệu lực.
3. **Spec §11.2 bước 1 và §12.1:** mất điện trong lúc đang xóa sector đầu của staging thì staging có thể còn nguyên và được cài lại. Từ lúc xóa xong thì không bao giờ cài lại. Ma trận `restore` giờ dùng staging v3 khác QSPI v2, và chỉ chấp nhận việc cài lại khi cắt điện đúng ở thao tác xóa đó.

Mọi test mới đều RED trước khi sửa. Kết quả sau khi sửa:
- toàn bộ host suite đạt (install 760, restore 298, seed 297 điểm cắt, mỗi ma trận thêm 300 lần chạy cắt nhiều lần);
- mutants 17/17;
- boot và app vẫn build.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt: còn lỗi Critical hoặc Important nào không? Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
