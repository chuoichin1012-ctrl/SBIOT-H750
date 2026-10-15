Tiếp tục trong cùng phiên. Plan 04c đã merge (PR #5). Tôi đã làm **Plan 04b**: đưa OTA và chống clone lên board thật. Code đã chạy và được kiểm trên board **trước khi** viết plan, giống cách làm Plan 02.

Branch `plan04b-ota-board`, commit `447c601`. Xem:
- `git diff 2fad42c..447c601 -- stm32/`;
- plan `stm32/docs/plans/2026-10-15-04b-ota-board.md` (phát hiện bench B1–B7, ruling 1–7);
- spec §6.2, §11.2, §12.4 và roadmap hàng 04b.

**Trình tự boot:**
1. watchdog, ngõ ra an toàn, clock;
2. QSPI ở chế độ indirect (`qf_nor_t`), rồi SPI4 (`spi_nor_t`);
3. `bind_check` (lỗi thì dừng, không có Reload);
4. lấy mẫu Reload 5 s;
5. `install_run`;
6. memory-mapped, rồi nhảy.

`INSTALL_HALT` thì nháy nhanh và đọc Reload. Bootloader không bao giờ rời memory-mapped.

**App:**
- kiểm `bind_valid` trước mọi thứ;
- lệnh console `o` (báo các image) và `u` (upload qua UART7 bằng `ota_stage_*`).

**Build:** `APP_VERSION` chỉ định nghĩa một chỗ, `app.img` do `mkimage.py` tạo sau mỗi lần build.

**Kiểm trên board:**
- `ota_check.py` đạt 17/17:
  - cài image mới;
  - reset 5 lần giữa lúc đang cài image 300 KB;
  - Reload (giả lập bằng cách ép PA8 qua debugger);
  - bản ghi của UID khác thì app bị xóa;
  - ghi lại bản ghi.
- `boot_check.py` của Plan 02, cả bản thường lẫn `--hostile`, đều đạt;
- host suite đạt, mutants 24/24.

**Phát hiện đáng chú ý:**
- **B1:** SPI4 đọc bị timeout trong app, nên bật `MasterReceiverAutoSusp`.
- **B3:** board rework phải được ghi bản ghi liên kết **trước** khi nạp, vì `flash.sh` reset board sau khi nạp, và bản ghi cũ sẽ xóa app vừa nạp.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt:
- Có lỗi nào trong code (driver SPI4 và QSPI, thứ tự boot, các vòng dừng, Reload, upload phía app) hay trong spec không? Có kịch bản board thật bị dừng, bị xóa nhầm, hoặc kẹt watchdog không?
- 7 ruling có chấp nhận được không?
- Có thiếu ca kiểm quan trọng nào trên board không?
- Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
