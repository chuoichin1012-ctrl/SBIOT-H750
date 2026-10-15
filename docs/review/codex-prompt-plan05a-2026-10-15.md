Tiếp tục trong cùng phiên. Plan 04b đã merge (PR #6). Tôi đã làm **Plan 05a**: logic lưu trữ trên FlashDB, chạy và kiểm trên host trước. Branch `plan05-storage`, xem:
- `git diff main..HEAD -- stm32/`;
- plan `stm32/docs/plans/2026-10-15-05a-storage-host.md` (phát hiện B1–B3, ruling 1–7);
- spec §5.3, §5.5, §9.1, §9.4 và roadmap hàng 05a/05b.

**Nội dung chính:**
- **FlashDB 2.2.0** được vendor, giống bản ESP32 đang dùng, cộng **một bản vá** (B1). Lúc mount, sau khi đánh dấu một KV ghi dở (`PRE_WRITE`), vòng duyệt khôi phục dừng lại (`return true`), nên giá trị cũ (`PRE_DELETE`) của KV đó không được khôi phục. Ma trận cắt điện đã bắt được lỗi này: cắt lúc đang ghi lại manifest trong lúc DB dọn rác thì mất manifest. `master` của upstream vẫn còn lỗi.
- **`components/store`**, port từ `store.c` của ESP32:
  - cache văn bản 20 KB;
  - `edgeDevices` ghi vào các bản ghi 8 KB theo generation tăng dần, manifest là điểm commit;
  - sau mỗi lần lưu, xóa mọi bản ghi không thuộc generation mới và không bị ghim;
  - lúc boot không có manifest thì không xóa gì (B2);
  - `GET` ghim generation;
  - cert, state, log TSDB.
- **Test:** `test_store` chạy trên bộ giả lập NOR:
  - cắt điện ở mọi thao tác khi lưu edge (89 điểm, có dọn rác) và khi lưu cấu hình nhỏ;
  - 150 lần chạy với 3 lần cắt;
  - một `GET` bị ghim qua 30 lần lưu;
  - cache đầy (bản build với cache 3800 B).

  Mutants 37/37, gồm cả mutant gỡ bản vá FlashDB.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt:
- Có lỗi đúng/sai, mất dữ liệu, rò lock hay sai lệch so với spec không? Bản vá FlashDB có đúng và đủ không, có khả năng lặp vô hạn không?
- 7 ruling có chấp nhận được không?
- Có thiếu ca test quan trọng nào không?
- Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
