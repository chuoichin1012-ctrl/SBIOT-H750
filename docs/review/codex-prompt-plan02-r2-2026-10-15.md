Vòng 2 cho Plan 02, cùng file `stm32/docs/plans/2026-10-14-02-board-bringup.md`, branch `plan02-bringup`. Từng điểm bạn nêu, cùng một phát hiện thêm, đã xử lý như sau:

1. **ES0392 §2.7.4** (tôi đã kiểm trong ES0392 Rev 15 mà người dùng tải về `docs/`).
   - Thêm `qf_errata_timing()`, viết đúng chuỗi thanh ghi của errata và tắt ngắt trong lúc chạy.
   - Gọi hàm này trong USER CODE `QUADSPI_MspInit 1` của bootloader. Ở đó kernel clock đã bật, và `HAL_QSPI_Init` cấu hình CR/DCR ngay sau đó.
   - `check_elf.py` có thêm gate: giữa lần ghi CCR đầu tiên và lần ghi CR tắt QUADSPI có tối đa 4 lệnh và không có lệnh nhánh. Ngân sách là 127 kernel clock, tức 0,79 µs ở 160 MHz.
   - Plan 04 gọi lại hàm này khi bootloader rời memory-mapped (ghi trong bảng B13).
2. **Phát hiện thêm, ES0392 §2.7.6:** với HCLK làm kernel clock, hệ số chia phải giữ duty 50 %. Bản thử chia 240 MHz cho 3 (lẻ), cho pha cao khoảng 4,2 ns, dưới tCLH 4,3 ns của GD25Q32E. Đổi QSPI sang pll1_q_ck 160 MHz (DIVQ1 = 6) với prescaler 1, nên vẫn 80 MHz nhưng chia chẵn. PLL1Q thuộc phần bootloader cố định. Đã cập nhật `.ioc`, `check_pins.py`, Global Constraints và đoạn sửa spec.
3. **Review Focus 1:** thêm `tools/openocd/qspi_hostile.tcl` và `boot_check.py --hostile`, mỗi ca 3 lần:
   - đưa flash vào continuous-read bằng lệnh EBh với M = A0h, rồi reset;
   - đang xóa sector 20h trên block dự trữ, đọc SR1 thấy WIP = 1, rồi reset.

   **Review Focus 2:** đã sửa lời hứa. Kiểm vector chỉ từ chối được image có vector hỏng. Image ghi dở mà vector còn đúng thì để cho SHA-256 của Plan 04, đã ghi trong mục Deviations.
4. **Đường dẫn OpenOCD:** `board.openocd()` tính đường dẫn Windows bằng `wslpath` trước khi `cwd=/mnt/c`, nên không còn `$PWD` sau `cd`.
5. **BOR:** sửa mô tả thành VBOR2 2,31 V cạnh xuống và 2,41 V cạnh lên (DS12556 bảng 25, tôi đã kiểm).
6. **Đồng bộ và dừng khi lỗi:**
   - §14 `system_app.c` thêm vào script Task 1, nay là 15 + 2 thay thế, đã dry-run;
   - kiểm phiên bản in `NOT PINNED` rồi dừng;
   - build ở Task 4/5 và mutant Task 5 nối bằng `&&`, nên không bao giờ kiểm ELF cũ.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt: còn lỗi chặn nào không? Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`, dòng thứ hai là `THỰC THI: <SUBAGENT | NATIVE>`.
