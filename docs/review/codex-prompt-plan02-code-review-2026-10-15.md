Review cuối cho Plan 02 (`stm32/docs/plans/2026-10-14-02-board-bringup.md`), branch `plan02-bringup`, phạm vi `git diff 92bb009..HEAD`. Bỏ qua `stm32/firmware/third_party/` (vendor, đã khóa phiên bản) và phần CubeMX sinh trong `stm32/firmware/cube/`, chỉ xét các khối USER CODE và hai file `.ioc`.

Mã của chúng ta nằm ở các chỗ sau:
- `stm32/firmware/boot/`;
- `stm32/firmware/app/`;
- `stm32/firmware/components/board/`;
- `stm32/firmware/CMakeLists.txt` và `cmake/`;
- `stm32/firmware/tools/{mkioc.py,cubegen.sh,check_pins.py,test_check_pins.py,check_elf.py,flash.sh,openocd/}`;
- `stm32/firmware/test/bench/`;
- `stm32/firmware/test/host/test_board.c` và Makefile;
- `stm32/README.md`.

Đã chạy và đạt trên board thật:
- **`boot_check.py`, boot ấm:**
  - pin reset;
  - 10 lần reset phần mềm;
  - 3 lần IWDG1 reset, sau 1445–1537 ms kể từ lần kick cuối;
  - CRC của image đọc qua XIP trùng `app.bin`;
  - TIM2 khớp tick (9963/9963), `wdog` kick đúng 200 000 µs.
- **`--hostile`:** 3 lần reset khi flash ở continuous-read (EBh với M=A0, đọc được FFFFFFFF) và 3 lần reset khi đang xóa sector (SR1 03). Workaround §2.7.4 chạy trên MCU qua breakpoint.
- **Debugger dừng 5 s:** không reset (rsr=0, gap 5,2 s).
- **Slot QSPI trống:** từ chối nhảy, không reset.
- **BOR_LEV:** 0x0 → 0x2.
- **Boot nguội:** người dùng đã rút và cấp lại nguồn; kết quả bổ sung sau.

Các gate đã đạt:
- host 8 bộ;
- `check_pins` cộng 7 mutant;
- `check_elf`, 28 kiểm tra, và mutant `-O3` bị bắt (`__memset_veneer`);
- `check_schema`;
- cây ESP32 không đổi;
- chạy lại `cubegen` không làm thay đổi git.

Review Focus của plan (xét kỹ từng mục):
1. Reset ấm khi flash bận hoặc ở chế độ lạ.
2. QSPI chứa thứ không phải app.
3. GCC biến vòng chép của `Reset_Handler` thành `memcpy`/`memset`.
4. Debugger dừng lõi trong khi IWDG1 chạy.
5. Sửa `.ioc` làm hỏng ngầm hợp đồng với board.

Ruling và ghi chú trong ledger:
Task 2: Ruling: generated cube/boot/Src/quadspi.c has no USER CODE 'Includes' block (blocks: 0, QUADSPI_Init 0-2, MspInit 0-1, MspDeInit 0-1, 1) — Task 4 puts #include "qspi_flash.h" in USER CODE 0, right after the includes — cost if wrong: none (same place in the file).
Task 4 (for Task 5): Ruling: objdump prints the disabling QUADSPI_CR store in qf_errata_timing as 'str r2, [r3, #0]', so check_elf.errata_window must accept '[rN]' and '[rN, #0]' — the plan's regex only matches '[rN]' and would report 'pattern missing' — cost if wrong: none, the regex only widens to the same store.
Task 5: Ruling: Step 11 shows two complete boots — the first is stale output from flash.sh's reset, buffered by the ST-Link VCP while COM4 was closed and delivered on open; the plan's 'one boot' holds per reset. Task 6 will make board.listen discard the input buffer right after opening the port (DiscardInBuffer), else boot_check would count stale boots — cost if wrong: a line that arrives in the first ~50 ms after open is lost, and no check needs those.
Task 6: Ruling: board.listen started during() after a fixed 1.5 s, which raced PowerShell's port opening (pin-reset case saw no boot once); the PowerShell side now prints '0|__ready__' after Open+DiscardInBuffer and during() starts on that line — cost if wrong: none, listen only waits longer for a slow PowerShell.
Task 6: Note (evidence, not a ruling): a mutant bootloader without qf_init's 10-clock continuous-read exit also passed qspi_hostile_continuous — the first single-line command (66h) seen as address+M by a flash in continuous mode has M5-4 != 10b and ends that mode, so 66h is lost but 99h-less reset still yields a working JEDEC read. The hostile test proves recovery from a flash left in continuous-read mode (Review Focus 1); it does not prove the 10-clock exit is necessary, which stays as belt and braces per GD25Q32E §7.11.
Task 6: Ruling: --cold waits SBIOT_COLD_WAIT seconds (default 60, as planned) — the user reads the power-cycle request only after this turn ends, so the run uses a longer wait — cost if wrong: none, the check is unchanged.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt. Liệt kê lỗi theo mức Critical, Important, Minor, mỗi lỗi kèm file:dòng và kịch bản sai. Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
