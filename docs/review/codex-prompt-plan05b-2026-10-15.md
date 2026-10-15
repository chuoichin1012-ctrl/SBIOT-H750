Tiếp tục trong cùng phiên. Plan 05a đã merge (PR #7). Tôi đã làm **Plan 05b: lưu trữ trên board**, code chạy và được kiểm trên board trước. Branch `plan05b-storage-board`, xem:
- `git diff main..HEAD -- stm32/`;
- plan `stm32/docs/plans/2026-10-15-05b-storage-board.md` (phát hiện B1–B5, ruling 1–6);
- spec §5.4 (dòng SPI4), §9.1 và §12.4.

**Người dùng đã quyết:**
- cấu hình chỉ đọc một lần lúc khởi động, cập nhật thì phải khởi động lại;
- phạm vi chịu lỗi của lưu trữ: chịu được mất điện và lỗi đọc thoáng qua; lỗi phần cứng dai dẳng thì chỉ cần không làm hỏng thêm;
- không vá FlashDB thêm nữa;
- giữ dự án đơn giản.

**Nội dung:**
- **Driver SPI4** có port:
  - mutex dùng chung cho OTA và FlashDB;
  - `vTaskDelay(1)` trong lúc flash bận;
  - DMA cho pha dữ liệu ≥ 256 B, qua bộ đệm 4 KB non-cacheable ở SRAM1 (MPU region 2), `__DMB` hai phía, có abort khi timeout.

  CubeMX sinh DMA1 stream 0/1. Bootloader giữ polling.
- **`store`:** mount trong `init`.
- **`syslog`:** ring 32 KB ở SRAM4, ghi các dòng W/E vào flash, tối đa 30 dòng mỗi phút.
- **Heap:** newlib dùng heap_5 qua lớp bọc đã test trên host, và `check_elf` kiểm chỉ có một bộ cấp phát.
- **Sector cache của FlashDB đặt 16** (B1).
- **Console ngủ khi rảnh** (B3: vòng chờ cũ làm đói mọi task ưu tiên thấp hơn).

**Đo trên board (`store_check.py`, đạt):**
- 1000 lần lưu trong 12,2 s, CPU idle 71 %, khoảng hở lớn nhất của `wdog` 200 ms, không lỗi DMA;
- 5 lần reset giữa lúc lưu đều đạt;
- mount mất 0,2–1,8 s.

`ota_check` và `boot_check` (thường và `--hostile`) đều đạt; `test_store` và `test_heap` đạt.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt: trong phạm vi và các quyết định trên, có lỗi Critical hoặc Important nào không (driver DMA, đồng bộ, heap, syslog, thứ tự khởi tạo)? 6 ruling có chấp nhận được không? Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
