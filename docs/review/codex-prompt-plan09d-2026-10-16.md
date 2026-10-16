Plan 09c đã merge (PR #18). Sau khi xem số đo RAM của 09c, người dùng yêu cầu nâng giới hạn Edge lên **300 điểm**.

Plan nháp: `stm32/docs/plans/2026-10-16-09d-300-points.md`, trên branch `plan09d-300-points`.

**Các thay đổi chính:**
- record `STORE_EDGE_RECORD` 16 000 byte (một record một sector 16 KB);
- `CFG_EDGE_MAX`/`WEB_EDGE_MAX` 96 000;
- `MAX_NODES` 300, cả ở firmware và UI;
- UI kiểm kích thước body trước khi lưu;
- buffer report giữ ở 26 KB.

Không đổi layout flash. Record 8 KB của firmware cũ vẫn đọc được.

**Số đo 09c:** ở scenario (e), có TLS và 50 vòng, heap thấp nhất 142 240 B, khối lớn nhất 170 880 B. Tôi ước 300 điểm tốn thêm khoảng 43 KB heap cho model (216 B mỗi điểm). AXI còn khoảng 48 KB, đủ cho 2 buffer record tăng thêm khoảng 15,6 KB.

**Cần kiểm tra:**
- Các con số: một record 16 000 B cộng header FlashDB có vừa một sector 16 384 B không; 2 thế hệ 6 sector cộng 4 sector cho GC có đủ không.
- Mục "Known limits".
- Các ruling.
- Các task T1–T3.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa. Nhớ là người dùng muốn giữ đơn giản.
