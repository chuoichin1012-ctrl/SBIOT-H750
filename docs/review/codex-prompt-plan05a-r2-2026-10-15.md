Tiếp tục trong cùng phiên. Đã sửa theo review 05a trong commit `0c11b5c`, xem `git diff 3f9ec5b..0c11b5c -- stm32/` và mục "Codex round 1" trong plan doc. Mọi bản sửa đều có test RED trước.

1. **Ghi manifest báo lỗi:**
   - `store` mount lại DB `edge`, rồi lấy generation đã commit từ flash.
   - Ma trận mới `test_write_failures` cho từng thao tác trong 89 thao tác của một lần lưu edge (có dọn rác) lỗi một lần. Bất biến: model trước khi boot lại bằng model sau khi boot lại, và phải là đúng model cũ hoặc đúng model mới.
   - Ma trận này tìm ra op 86 (đúng kịch bản bạn nêu) và op 85: lần ghi trạng thái `WRITE` của manifest mới lỗi, rồi cache của FlashDB vẫn trả manifest mới.
2. **Generation mới đánh số cao hơn mọi bản ghi đang có**, nên không còn bước xóa trước commit. Manifest đọc vào bộ đệm cố định. Test: `test_lost_manifest`.
3. **Vòng lặp có giới hạn:**
   - vòng dọn của `store` tối đa 8 lượt;
   - FlashDB chỉ duyệt lại sau một lần ghi trạng thái thành công, tối đa 16 lần.

   Test: `test_no_endless_loops`, với `SIM_SILENT` và chốt chặn `alarm`.
4. **Thêm scope `port2`.** Sửa ngân sách §5.3.
5. **Thêm:** `test_gc_write_failures`, chạy riêng trên FlashDB (một chuỗi ghi đè có dọn rác, cho từng thao tác lỗi một lần), tìm ra **hai lỗi FlashDB nữa**, cả hai làm mất một KV còn sống. Cả hai đã vá trong `third_party/patches/flashdb-2.2.0-sbiot.patch`:
   - `move_kv` bỏ qua lỗi chép và lỗi ghi trạng thái `WRITE` rồi vẫn xóa bản gốc, sau đó `do_gc` format sector;
   - `move_kv` bỏ qua lỗi ghi trạng thái của sector đích.

Mutants 44/44, gồm từng chỗ sửa ở trên. Host suite đạt.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt: còn lỗi Critical hoặc Important nào không? Bản vá FlashDB (ba chỗ) có tạo lỗi mới không? Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
