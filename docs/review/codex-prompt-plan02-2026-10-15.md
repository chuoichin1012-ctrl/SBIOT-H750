Tiếp tục trong cùng phiên. Plan 03 đã merge (PR #2). Tôi đã viết **Plan 02** (đưa board lên chạy): `stm32/docs/plans/2026-10-14-02-board-bringup.md`, nằm trên branch `plan02-bringup`.

Toàn bộ code trong plan đã chạy thử trên board thật trước khi viết plan. Kết quả đo:
- **Bootloader** (18 KB, flash nội):
  - PLL1 480 MHz, QSPI bank 2 ở 80 MHz, lệnh EBh, memory-mapped, đọc đúng JEDEC `c84016`;
  - từ chối firmware cũ, rồi nhảy vào app.
- **App XIP từ QSPI:**
  - trạng thái bàn giao đúng, region MPU 0–1 khớp với bootloader;
  - kernel FreeRTOS, `memcpy` và `memset` chạy từ ITCM;
  - CRC-32 của image đọc qua XIP trùng `app.bin`;
  - `wdog` kick đúng 200 000 µs; `now_us` (TIM2) khớp tick của FreeRTOS.
- **Reset:**
  - reset phần mềm và reset bằng IWDG1 (khoảng 1,6 s sau lần kick cuối) đều boot lại đủ;
  - debugger dừng lõi 5 s thì không reset.

Bảng B1–B12 trong plan ghi các phát hiện trên bench, mỗi dòng kèm bằng chứng. Các quyết định cần bạn xét:

1. **R20 (SGM706 WDO → MR) chưa lắp**, người dùng đã xác nhận. Ngừng kick 60 s mà board không reset. Đề xuất:
   - bootloader bật IWDG1 (LSI/32, reload 1600) và đóng băng nó khi debug;
   - "kick" luôn là đảo WDI cộng nạp lại IWDG1; "ngừng kick" là ngừng cả hai.

   Việc này sửa spec §11.2 hàng 2, §11.3 và §11.4 (Task 1).
2. **Bootloader chỉ cố định PLL1, bus, VOS, flash latency và QSPI.** PLL2/PLL3 và các mux kernel clock do app cấu hình trong MspInit mà CubeMX sinh ra (PLL2P cho ADC). Lý do: bootloader không cập nhật được qua OTA. Việc này sửa §6.2.
3. **Hai file `.ioc` (`boot.ioc`, `app.ioc`), sinh mã headless bằng `cubegen.sh`.**
   - App giữ `main()` của CubeMX nhưng tắt lời gọi `SystemClock_Config`, `MX_QUADSPI_Init` và `MPU_Config`.
   - App giữ bảng vector trong file startup của CubeMX nhưng thay `Reset_Handler` (vốn là weak). App không có `system_app.c` riêng: `SystemInit` và `ExitRun0Mode` bị loại khỏi `app.elf`, có gate kiểm.
   - Timebase của HAL là `now_us()`, nên SysTick thuộc FreeRTOS.
4. **HAL v1.11.6, CMSIS device v1.10.7, CMSIS core 5.9.0 lấy từ GitHub và khóa phiên bản, thay cho bản của gói CubeMX 1.12.0.** Gói đó có `D1CorePrescTable` sai, đo được baud gấp đôi (B3).
5. **QSPI dùng FSIZE = 22 cho chip 4 MB** (errata ES0396 §2.4.4, B5). Chân QSPI đặt tốc độ very high (B4). Lệnh thoát continuous read dùng pha alternate 32 bit (B6).
6. **Nạp QSPI bằng CubeProgrammer với loader của nhóm** (khóa SHA-256, đã chứng minh trên block 0x903E0000), vì `stmqspi` không chạy (B7). Loader tự viết dời sang Plan 04. Loader của nhóm không kick WDI, nên chỉ an toàn khi R20 chưa lắp.

Lệch spec có chủ đích, đã ghi trong plan:
- `init` còn sống làm console bench;
- heap và bản ghi HardFault để cho Plan 05;
- kiểm header image để cho Plan 04.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt:
- Plan 02 có bước nào sai, mơ hồ, hoặc lệch spec mà chưa nêu không? Script sửa spec ở Task 1 có đúng ý không? Có thiếu ca test bench nào quan trọng không?
- Sáu quyết định trên có chấp nhận được không?
- Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`, dòng thứ hai là `THỰC THI: <SUBAGENT | NATIVE>`.
