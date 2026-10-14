# Plan 02: Board bring-up (bootloader stub and XIP app), implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The board boots our own bootloader from internal flash, which hands over to a FreeRTOS app executing in place from QSPI bank 2. Both images are built from CubeMX-generated init code. Cold and warm boots are proven on the bench, and so is the watchdog reset.

**Architecture:**
- **Two CubeMX projects** (`cube/boot/boot.ioc`, `cube/app/app.ioc`), generated headless in WSL by `tools/cubegen.sh`. Our code enters only through USER CODE blocks.
  - The bootloader runs CubeMX's `main()`: MPU (QSPI regions), HAL, clocks (PLL1 480 MHz), GPIO safe levels, IWDG1, QUADSPI, UART7. Then `boot_main()` resets the GD25Q32E, maps it with Quad I/O Fast Read (EBh) at 80 MHz, checks the app vector table and jumps.
  - The app keeps CubeMX's vector table but replaces its weak `Reset_Handler`. Ours enables the FPU and the SRAM clocks, copies the data and ITCM sections, programs the whole MPU once, enables the caches, then calls CubeMX's `main()`. `SystemClock_Config`, `MX_QUADSPI_Init` and `MPU_Config` are never called there. `app_main()` starts `wdog` and `init` under FreeRTOS.
- **One HAL** for both images, vendored from the pinned GitHub releases rather than from the FW pack 1.12.0 (whose `system_stm32h7xx.c` miscomputes every APB clock).
- **Gates:**
  - `tools/check_pins.py`: both `.ioc` files against spec §3 and the CubeMX MCU database;
  - `tools/check_elf.py`: section placement, plus "the app never touches clocks, QSPI or the MPU outside `Reset_Handler`";
  - host tests for the hardware-free parts;
  - `test/bench/boot_check.py` on the board: pin, software and watchdog resets, the XIP image CRC, TIM2 against the RTOS tick, and a cold boot.

**Tech Stack:** C11 (gnu11), xPack arm-none-eabi-gcc 15.2.1, CMake 3.28 + Ninja, STM32CubeMX 6.13 (headless, Linux Java), STM32H7 HAL v1.11.6, CMSIS device H7 v1.10.7, CMSIS core 5.9.0, FreeRTOS-Kernel V11.3.1, STM32CubeProgrammer CLI, Python 3, PowerShell (COM port on Windows).

**Spec:** `stm32/docs/specs/2026-10-14-stm32-firmware-design.md` (§3, §4, §5.1, §5.3, §6.2, §6.3, §11.1, §11.2 boot sequence, §11.4, §12.4, §14), amended by Task 1. **Roadmap:** `stm32/docs/plans/2026-10-14-00-roadmap.md`, row 02.

## What the bench showed before this plan (2026-10-14)

A prototype of exactly the code below was built, flashed and run on the board. Its results drive several decisions, and the spec changes in Task 1 follow from them.

| # | Finding | Evidence | Consequence |
|---|---|---|---|
| B1 | **R20 (SGM706 WDO → MR) is not fitted.** A timeout cannot reset the MCU | The user confirmed it. A spike that stopped kicking WDI ran 60 s without a reset | IWDG1 is the reset source. WDI is still toggled, for the day R20 is fitted |
| B2 | The board's UART7 output is on **COM4**, not COM5 | Spike output arrived on COM4; COM5 stayed silent | Bench tools default to COM4 (`SBIOT_COM`) |
| B3 | FW pack 1.12.0's `system_stm32h7xx.c` has `D1CorePrescTable = {0,0,0,0,0,0,0,0,1,…}`, so HAL reads APB ÷2 as ÷1 | UART7 BRR 0x823 (2083) at PCLK1 = 120 MHz, so 57 600 baud. cmsis-device-h7 v1.10.7 has `{0,0,0,0,1,2,3,4,1,…}` | Both images use the v1.10.7 system file |
| B4 | CubeMX leaves the QSPI pins at **low speed**, and every QSPI read then times out at 80 MHz | JEDEC read 0, RX FIFO empty, HAL timeout | `GPIO_Speed=VERY_HIGH` on the six QSPI pins |
| B5 | **ES0396 §2.4.4**: a memory-mapped read of the last byte of the FSIZE region returns 0x00, and a repeated read stalls AXI | Verify of 0x903F0000–0x903FFFFF failed at 0x903FFFFF only. A block in the middle verified | FSIZE = 22 (8 MB) for the 4 MB part. MPU region 1 stays 4 MB |
| B6 | Sending ten all-ones clocks with an address phase of 0xFFFFFF sets TEF (address beyond FSIZE) | QUADSPI SR TEF = 1 | The continuous-read exit uses a 32-bit alternate-byte phase instead |
| B7 | OpenOCD `stmqspi` cannot read the flash ("Failed to read memory at 0x52005020") | Raw register JEDEC reads work | Flash QSPI with CubeProgrammer and the team's loader `STM32H750VBT6_GD25Q32ESIG.stldr` (SHA-256 `5ef2de36…0699`). Erase, program and verify proven on 0x903E0000. OpenOCD is for debugging only (`QUADSPI 0`) |
| B8 | The team's loader never touches GPIO, so it does not kick WDI | Disassembly: no GPIO writes | Safe while R20 is missing. Plan 04 brings our own loader, which kicks |
| B9 | With the core halted and WDI static, nothing resets the board | OpenOCD `reset halt`, PC10 toggled, then 5 s static high and low: RSR stays 0 | Consistent with B1 |
| B10 | CubeMX's own startup file declares `Reset_Handler` **weak** and calls `ExitRun0Mode` then `SystemInit` | `startup_stm32h750xx.s` | The app overrides it. `check_elf.py` proves `SystemInit` and `ExitRun0Mode` are gone from `app.elf` |
| B11 | newlib-nano `printf` has no `%llu` | Output "lu" and shifted arguments | 64-bit values are printed as 32-bit milliseconds |
| B12 | CubeMX's first generation of a fresh `.ioc` may put the toolchain files under `Projects/`. Once `ToolChainLocation` is empty, generation is idempotent | Four generations observed | `cubegen.sh` regenerates until the files sit next to `Src/`. The gate is a second run with no diff |

Prototype numbers on the board:
- **Bootloader:** 18 080 B. The boot-to-app-ready time is 13 ms.
- **App:** 27 020 B in QSPI, 7.6 KB in ITCM, 17.5 KB of DTCM including the MSP.
- **XIP CRC:** the CRC-32 of the image read through XIP equals that of `app.bin`. Over 27 KB it takes 2.3 ms.
- **Watchdog:** `wdog` kicks every 200 000 µs exactly. IWDG1 resets about 1.6 s after the last kick.

## Global Constraints

- **Clocks** (bootloader, frozen):
  - HSE 25 MHz crystal; PLL1 M=5 N=192 P=2 Q=4 R=2, giving SYSCLK 480 MHz;
  - VOS0, LDO supply, flash latency 4;
  - AHB ÷2 (240 MHz), APB1–4 ÷2 (120 MHz each);
  - QSPI kernel clock = HCLK3 240 MHz.
- **Clocks** (app): PLL2 M=5 N=80 P=20 gives 20 MHz for ADC3, configured by the app's `HAL_ADC_MspInit` (Plan 07). USART and SPI kernel clocks = PCLK.
- **QSPI:**
  - bank 2 (`FSEL`=1), FSIZE 22, prescaler 2 (80 MHz ≤ 104 MHz at DC=0);
  - half-cycle sample shift, CS high time 2 cycles;
  - read command EBh: address on 4 lines, then alternate byte M = FFh on 4 lines, then 4 dummy clocks (8 when SR3.DC = 1);
  - QE set only when clear; DC never written;
  - before configuring, send 10 all-ones clocks, then 66h and 99h, then wait 13 ms.
- **Memory map:**
  - header 1 KB at 0x90000000, vectors at 0x90000400, slot 1 472 KB;
  - bootloader ≤ 64 KB at 0x08000000;
  - MSP 4 KB at the top of DTCM; DTCM ≤ 120 KB;
  - `.itcm_text` from 0x00000400.
- **MPU** (app), exactly these regions:
  - 0: the CubeMX background region, 4 GB, SRD 0x87, no access, XN;
  - 1: 0x90000000, 4 MB, read-only, cacheable, executable;
  - 2: SRAM1 0x30000000, 128 KB, Normal non-cacheable, XN;
  - 3: 0x3800FC00, 1 KB, Normal non-cacheable, XN;
  - 4: 0x00000000, 1 KB, no access.

  Regions 0 and 1 equal the bootloader's.
- **Watchdogs:** IWDG1 LSI/32, reload 1600 (≈ 1.6 s), started by the bootloader and frozen while the core is halted. `board_kick()` toggles WDI (PC10) and reloads IWDG1. `wdog` calls it every 200 ms at priority 31.
- **Safe levels** after `MX_GPIO_Init`:
  - PD15 (OE) high; PD10, PD11, PD14 low;
  - PA4, PA5 low; PD13 (ETH_RESET) low; PE4 (SPI4 NSS) high;
  - then `board_latch_zero()`.
- **Code style:**
  - our C: `-std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror`, 4-space indent, `//` comments;
  - vendored and generated code builds with defaults;
  - host tests: the existing `test/host` flags (ASan, fatal UBSan, `-Werror`).
- **Generated code** is edited only inside USER CODE blocks; everything else changes through the `.ioc` and `tools/cubegen.sh`.
- **Logs:** UART7, 115200 8N1.
- **Working directory:** run every command from the repository root `/home/linh/projects/SBIOT-Gateway`, with `source ~/opt/xpack/env.sh` done.
- **Windows-side files:** files for Windows programs go under `/mnt/c/Users/linh_bui/sbiot/`. The sandbox's `/tmp` is invisible to Windows.

## Deviations from the spec, deliberately left to later plans

- **`init` stays alive.** Spec §6.3 has `init` delete itself after boot. In Plan 02 it reports the boot and then stays as the UART7 bench console (`i`, `r`, `w`, `c`). Plan 05 (syslog) replaces it, and `init` then deletes itself as specified.
- **No heap yet.** `board_early()` (spec §6.3) does not set up a heap, because Plan 02 creates only static objects (`configSUPPORT_DYNAMIC_ALLOCATION 0`). Plan 05 adds `heap_5` with the newlib wrapper. `_sbrk` already always fails.
- **No HardFault record yet.** The HardFault record in backup SRAM (spec §11.4) is not part of Plan 02. Until then, a fault hangs in CubeMX's handler and IWDG1 resets the board. The `.bkp_noinit` section exists for that record.
- **Header checks come later.** Plan 02 checks only the MSP and the reset vector before jumping. The header checks of §11.1 (magic, CRC, hardware id, size, SHA-256) come with `mkimage.py` in Plan 04.

## Review Focus

1. **A warm reset while the flash was busy or in an odd mode** (reset during XIP, during a status-register write, after another firmware). The bootloader must still identify the flash and map it. Pinned in Task 6: 10 back-to-back software resets and 3 IWDG resets, each must reach the app.
2. **QSPI holding something that is not our app** (the old firmware, erased flash, half an image). The bootloader must refuse to jump and stay in halted rescue, still feeding both watchdogs. Pinned in Task 3, `test_jumpcheck` (includes the words the bench QSPI held), and in Task 6, the first boot before `app.bin` is flashed.
3. **GCC turning `Reset_Handler`'s copy loops into `memcpy`/`memset` calls**, which run from ITCM before ITCM is filled. Pinned in Task 5: `check_elf.py` plus an `-O3` mutant that must fail it, including calls through `__memset_veneer`.
4. **A debugger halting the core while IWDG1 runs.** The board must not reset under the debugger, and flashing (connect under reset) must still work after the app runs. Pinned in Task 6, halt-for-5-s step.
5. **A `.ioc` edit that silently breaks the board contract** (pin swapped, PLL1 changed in one file only, latch enabled at reset, FSIZE back to 21, QSPI pins slow). Pinned in Task 2, `test_check_pins.py` with one mutation per case.

## Files

| Path | Responsibility | Task |
|---|---|---|
| `stm32/docs/specs/2026-10-14-stm32-firmware-design.md`, `stm32/docs/plans/2026-10-14-00-roadmap.md` | Spec amendments (B1, B3–B7), roadmap rows 02 and 04 | 1 |
| `stm32/firmware/third_party/stm32cube_h7/` | HAL v1.11.6, CMSIS device v1.10.7 (3 headers, system template), CMSIS core 5.9.0 headers, licences, README | 2 |
| `stm32/firmware/third_party/FreeRTOS-Kernel/` | Kernel V11.3.1: `*.c`, `include/`, `portable/GCC/ARM_CM4F/`, `portable/MemMang/heap_5.c`, licence | 2 |
| `stm32/firmware/tools/mkioc.py` | How the two `.ioc` files were authored (reasons for every non-default value) | 2 |
| `stm32/firmware/cube/boot/`, `stm32/firmware/cube/app/` | `.ioc` + CubeMX output (`Inc/`, `Src/`, startup, linker script) | 2 |
| `stm32/firmware/tools/cubegen.sh` | Headless regeneration | 2 |
| `stm32/firmware/tools/check_pins.py`, `tools/test_check_pins.py` | Pin/clock gate and its mutation test | 2 |
| `stm32/firmware/components/board/{tick64,crc32}.c`, `include/{tick64,crc32}.h`, `stm32/firmware/boot/jumpcheck.{c,h}`, `test/host/test_board.c` | Hardware-free code and its host test | 3 |
| `stm32/firmware/CMakeLists.txt`, `CMakePresets.json`, `cmake/arm-none-eabi.cmake` | Cross build of both images | 4 (boot), 5 (app) |
| `stm32/firmware/boot/{boot,qspi_flash}.{c,h}`, `components/board/board_io.c`, `include/board.h` | Bootloader stub, GD25Q32E driver, watchdog kick, latch | 4 |
| `stm32/firmware/app/*`, `components/board/{now_us,wdog,blog,syscalls}.c` + headers | XIP app startup, MPU, linker script, FreeRTOS config, tasks, log | 5 |
| `stm32/firmware/tools/check_elf.py` | Layout and ownership gate | 5 |
| `stm32/firmware/tools/flash.sh`, `test/bench/{board,boot_check}.py`, `stm32/firmware/README.md` (section) | Flashing and the bench check | 6 |

---

### Task 1: Spec and roadmap amendments

The bench findings B1, B3–B7 change what the spec says about the watchdog, the clock ownership, the QSPI parameters and the flashing route. This task records them, so that every later task argues from the amended spec.

**Files:**
- Modify: `stm32/docs/specs/2026-10-14-stm32-firmware-design.md` (§4 tree, §6.2, §11.2 row 2, §11.3, §11.4, §12.4, §14)
- Modify: `stm32/docs/plans/2026-10-14-00-roadmap.md` (rows 02 and 04)

**Interfaces:** none (documentation).

- [ ] **Step 1: Apply the amendments with a checked script**

Each replacement must match exactly once; the script stops otherwise.

```bash
python3 - <<'EOF'
import sys
SPEC = 'stm32/docs/specs/2026-10-14-stm32-firmware-design.md'
ROAD = 'stm32/docs/plans/2026-10-14-00-roadmap.md'
edits = {SPEC: [
 ("├── cube/             .ioc + code CubeMX sinh ra. Chỉ sửa trong các khối USER CODE",
  "├── cube/             boot/boot.ioc, app/app.ioc + code CubeMX sinh ra (tools/cubegen.sh, CubeMX chạy headless). Chỉ sửa trong các khối USER CODE"),
 ("- Toàn bộ cây clock: HSE 25 MHz, PLL1 làm SYSCLK, PLL2 và PLL3 cấp kernel clock cho USART, SPI, ADC. Mục tiêu 480 MHz với chip rev V (VOS0).",
  "- Phần cây clock mà XIP phụ thuộc: HSE 25 MHz, PLL1 làm SYSCLK (M=5, N=192, P=2: 480 MHz với chip rev V, VOS0, nguồn LDO, flash latency 4), AHB ÷2 (240 MHz), APB1–4 ÷2 (120 MHz). Bootloader nằm trong flash nội và không cập nhật qua OTA, nên phần nó cố định phải nhỏ nhất có thể: PLL2/PLL3 và các bộ chọn kernel clock của ngoại vi do app cấu hình trong các hàm MspInit mà CubeMX sinh ra (ví dụ PLL2P 20 MHz cho ADC3 trong `HAL_ADC_MspInit`). USART và SPI dùng PCLK làm kernel clock.\n- IWDG1 (LSI/32, reload 1600 ≈ 1,6 s), đóng băng khi debugger dừng lõi (`DBGMCU_APB4FZ1.DBG_IWDG1`). Lý do ở §11.4."),
 ("  - lệnh đọc Quad I/O Fast Read (`0xEB`) với số dummy cycle theo datasheet ở tần số đã chọn;\n  - prescaler sao cho clock QSPI ≤ giới hạn của GD25Q32E ở 3,3 V;\n  - trước khi cấu hình, gửi Reset Enable và Reset (`0x66`, `0x99`) để đưa flash về trạng thái đã biết, vì MCU có thể vừa reset trong lúc flash đang ở một chế độ khác;",
  "  - lệnh đọc Quad I/O Fast Read (`0xEB`): địa chỉ 4 dòng, byte M = `0xFF` trên 4 dòng (M5-4 ≠ 10, flash không bao giờ vào continuous read mode), rồi 4 dummy clock khi bit DC (SR3 bit 0) = 0, 8 khi DC = 1 (GD25Q32E Rev 1.3 §6, §7.11). DC không bao giờ được ghi;\n  - prescaler 2: 240 / 3 = 80 MHz, dưới giới hạn 104 MHz của GD25Q32E khi DC = 0. Sample shift nửa chu kỳ, CS high 2 chu kỳ (tSHSL ≥ 20 ns). Các chân QSPI đặt tốc độ very high (với tốc độ low, mọi lệnh đọc đều timeout ở 80 MHz);\n  - **FSIZE = 22 (8 MB) cho chip 4 MB**: theo ES0396 §2.4.4, đọc memory-mapped byte cuối của vùng FSIZE luôn trả 0x00 và đọc lại làm treo bus AXI; region MPU 1 vẫn chỉ 4 MB nên phần dư không truy cập được;\n  - trước khi cấu hình: 10 clock với IO0–IO3 ở mức 1 (lệnh `0xFF` 4 dòng cộng 32 bit alternate byte, không dùng pha địa chỉ vì địa chỉ ngoài FSIZE làm QUADSPI báo TEF) để thoát continuous read mode, rồi Reset Enable và Reset (`0x66`, `0x99`) và chờ 13 ms (tRST_E 12 ms), vì MCU có thể vừa reset trong lúc flash đang ở một chế độ khác;"),
 ("- Region MPU của QSPI.",
  "- Region MPU của QSPI (`MPU_Config` do CubeMX sinh từ `boot.ioc`): region 0 nền mặc định của CubeMX (4 GB, tắt sub-region 0, 1, 2, 7, nên 0x60000000–0xDFFFFFFF không truy cập được và không bị fetch suy đoán) và region 1 là cửa sổ QSPI 4 MB, chỉ đọc, cacheable, thực thi được."),
 ("- **Startup và `SystemInit` riêng cho XIP.** Startup GCC chuẩn của ST gọi `SystemInit()` *trước* `main()`, và hàm này reset RCC, tắt PLL. Vì vậy app dùng `system_app.c` riêng, không đụng tới RCC, QSPI và pinmux.",
  "- **Startup riêng cho XIP.** Startup GCC chuẩn của ST gọi `ExitRun0Mode()` và `SystemInit()` *trước* `main()`, và các hàm này đổi nguồn cấp, reset RCC, tắt PLL. File startup của CubeMX khai báo `Reset_Handler` là weak: app giữ bảng vector của file đó và thay `Reset_Handler` bằng `app/startup_app.c`. `SystemInit` và `ExitRun0Mode` không còn trong `app.elf` (gate `tools/check_elf.py`). App vẫn dùng `system_stm32h7xx.c` (bản cmsis-device-h7 v1.10.7) chỉ để có `SystemCoreClockUpdate()`, hàm này chỉ đọc RCC."),
 ("  1. bật clock SRAM D2/D3;",
  "  0. bật FPU (`CPACR`), trước mọi code có thể dùng FPU;\n  1. bật clock SRAM D2/D3;"),
 ("- Trong `.ioc` của app, đặt \"Do not generate function call\" cho `SystemClock_Config` và `MX_QUADSPI_Init`. HAL sẽ từ chối đổi PLL1 khi nó đang là SYSCLK, và khởi tạo lại QUADSPI sẽ làm hỏng XIP.",
  "- Trong `app.ioc`, đặt \"Do not generate function call\" cho `SystemClock_Config`, `MX_QUADSPI_Init` và `MX_CORTEX_M7_Init` (MPU). HAL sẽ từ chối đổi PLL1 khi nó đang là SYSCLK, khởi tạo lại QUADSPI sẽ làm hỏng XIP, và MPU chỉ được lập một lần trong `Reset_Handler`. Timebase của HAL trong app là `now_us()` (TIM2): `HAL_InitTick` không làm gì, `HAL_GetTick` = `now_us()/1000`; SysTick, SVC và PendSV thuộc FreeRTOS (CubeMX không sinh ba handler này cho app)."),
 ("| 2 | Thao tác chép dữ liệu (xóa, ghi page, đọc lại) quá deadline, tức 2 lần giá trị tối đa trong datasheet GD25Q32E, ở **bất kỳ** đường chép nào: có checkpoint (state machine, hàng 10) hoặc theo dõi tiến độ trong RAM (hàng 6) | **Ngừng kick**, SGM706 reset board.",
  "| 2 | Thao tác chép dữ liệu (xóa, ghi page, đọc lại) quá deadline, tức 2 lần giá trị tối đa trong datasheet GD25Q32E, ở **bất kỳ** đường chép nào: có checkpoint (state machine, hàng 10) hoặc theo dõi tiến độ trong RAM (hàng 6) | **Ngừng kick** (WDI và IWDG1): IWDG1 reset board (và SGM706 nếu R20 được lắp, §11.4)."),
 ("thì `wdog` ngừng kick. Board reset và `trial` tăng lên.",
  "thì `wdog` ngừng kick (WDI và IWDG1). IWDG1 reset board và `trial` tăng lên."),
 ("- **SGM706:** timeout tối thiểu 1,0 s, danh định 1,6 s (datasheet SGM706, trang 4). `wdog` kick mỗi 200 ms.",
  "- **SGM706 và IWDG1:** SGM706 có timeout tối thiểu 1,0 s, danh định 1,6 s (datasheet SGM706, trang 4). **Trên board hiện tại R20 (WDO → MR) chưa lắp**, nên SGM706 hết thời gian cũng không reset được MCU (đã thử trên bench: ngừng kick 60 s không reset). Vì vậy bootloader bật IWDG1 (LSI/32, reload 1600: 1,5–1,74 s theo dải LSI), và \"kick\" luôn nghĩa là đảo WDI **và** nạp lại IWDG1 (`board_kick()`); \"ngừng kick\" là ngừng cả hai. IWDG1 không tắt được cho tới lần reset sau và được đóng băng khi debugger dừng lõi. Khi R20 được lắp, cả hai cùng hoạt động. `wdog` kick mỗi 200 ms. **Đề xuất cho phần cứng:** lắp R20."),
 ("- Nạp lần đầu qua SWD (P6) bằng OpenOCD với flash bank `stmqspi`, QSPI bank 2. Script phải đặt `FSEL`=1 trước khi probe. Flash nội nạp cùng lúc. Đường thay thế là STM32CubeProgrammer, nhưng cần một external loader viết riêng cho board.",
  "- Nạp qua SWD (P6) bằng STM32CubeProgrammer CLI (`tools/flash.sh`, chạy từ WSL): flash nội nạp trực tiếp; QSPI bank 2 qua external loader. OpenOCD `stmqspi` không đọc được flash trên board này (\"Failed to read memory at 0x52005020\"), nên OpenOCD chỉ dùng để debug, với `QUADSPI 0`.\n- Plan 02 dùng loader của nhóm `STM32H750VBT6_GD25Q32ESIG.stldr` (Dev_info: 0x90000000, 4 MB, page 256 B, 1024 sector 4 KB; SHA-256 `5ef2de362969bb2ad9d0c92604e58f27b018614533fe421e6ac5974c9bcb0699`), sau khi chứng minh xóa, ghi và đọc lại trên vùng dự trữ 0x903E0000. Loader này không kick WDI: chỉ an toàn khi R20 chưa lắp. Plan 04 thay bằng loader build từ mã nguồn (cùng driver QSPI với bootloader, kick WDI và IWDG1 trong các vòng chờ)."),
 ("| Cấu hình QSPI: QE, opcode, dummy cycle, prescaler | Lấy từ datasheet GD25Q32E. Kiểm XIP ở tần số đích khi bring-up |",
  "| Cấu hình QSPI: QE, opcode, dummy cycle, prescaler | Lấy từ datasheet GD25Q32E Rev 1.3 (§6.2). Đã kiểm XIP ở 80 MHz trên bench: CRC của image đọc qua XIP trùng `app.bin` |"),
 ("| Hậu tố của SGM706 trên BOM | Chốt cùng nhóm phần cứng |",
  "| Hậu tố của SGM706 trên BOM; R20 chưa lắp | Chốt cùng nhóm phần cứng. Tới khi R20 được lắp, IWDG1 là nguồn reset duy nhất (§11.4) |"),
 ("| OpenOCD `stmqspi` trên bank 2 | Script đặt `FSEL`=1 trước khi probe. Kiểm ngay ở bước bring-up đầu tiên |",
  "| OpenOCD `stmqspi` trên bank 2 | Không chạy trên board này (lỗi đọc 0x52005020). Dùng STM32CubeProgrammer với external loader (§12.4) |\n| Lỗi trong gói CubeMX H7 1.12.0: `D1CorePrescTable` của `system_stm32h7xx.c` đọc APB ÷2 thành ÷1 (mọi baud và kernel clock sai gấp đôi) | Dùng HAL v1.11.6, CMSIS device v1.10.7, CMSIS core 5.9.0 từ GitHub, khóa phiên bản trong `third_party/stm32cube_h7` |"),
], ROAD: [
 ("| 02 | Board bring-up: bootloader stub and XIP app | CubeMX `.ioc`, CMake cross build, bootloader that sets up clock and QSPI bank 2 then jumps, app with its own `Reset_Handler`/`SystemInit`, MPU, cache, ITCM, `wdog`, **`now_us()` (TIM2 at 1 MHz, extended to 64 bits)**, log on UART7, OpenOCD `stmqspi` flashing. Verified by cold and warm boot on the board and by disassembly |",
  "| 02 | Board bring-up: bootloader stub and XIP app | `boot.ioc`/`app.ioc` generated headless (`cubegen.sh`, `check_pins.py`), CMake cross build, bootloader that sets up PLL1, IWDG1 and QSPI bank 2 then jumps, app with its own `Reset_Handler`, MPU, cache, ITCM, `wdog`, **`now_us()` (TIM2 at 1 MHz, extended to 64 bits)**, log on UART7, flashing with STM32CubeProgrammer and the team's loader. Verified by cold and warm boot on the board (`boot_check.py`) and by `check_elf.py` |"),
 ("| 04 | Bootloader OTA state machine and SPI4 driver | `boot/install.c` (decision table rows 1–10, RAM-progress path, handoff block), NOR simulator with partial writes, SPI4 driver, `mkimage.py`, app-side `PENDING`/confirm/409 |",
  "| 04 | Bootloader OTA state machine and SPI4 driver | `boot/install.c` (decision table rows 1–10, RAM-progress path, handoff block), NOR simulator with partial writes, SPI4 driver, `mkimage.py`, app-side `PENDING`/confirm/409, **our own CubeProgrammer external loader** (QSPI erase/program from `boot/qspi_flash.c`, kicks WDI and IWDG1) |"),
]}
for path, pairs in edits.items():
    text = open(path, encoding='utf-8').read()
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            sys.exit('%s: expected 1 match, found %d for: %s' % (path, n, old[:70]))
        text = text.replace(old, new)
    open(path, 'w', encoding='utf-8').write(text)
print('amendments: %d spec, %d roadmap' % (len(edits[SPEC]), len(edits[ROAD])))
EOF
```

Expected: `amendments: 14 spec, 2 roadmap`.

- [ ] **Step 2: Check the result**

Run: `grep -c "IWDG1" stm32/docs/specs/2026-10-14-stm32-firmware-design.md; grep -c "stmqspi" stm32/docs/specs/2026-10-14-stm32-firmware-design.md`
Expected: the first count ≥ 6, the second exactly 2 (§12.4 and §14, both saying it does not work here).

- [ ] **Step 3: Commit**

```bash
git add stm32/docs/specs/2026-10-14-stm32-firmware-design.md stm32/docs/plans/2026-10-14-00-roadmap.md
git commit -m "stm32 spec: IWDG1 as reset source (R20 not fitted), app owns PLL2, QSPI FSIZE 22 (ES0396), CubeProgrammer flashing"
```

### Task 2: Vendored HAL and FreeRTOS, the two CubeMX projects, and the pin gate

**Files:**
- Create: `stm32/firmware/third_party/stm32cube_h7/` (copied), `stm32/firmware/third_party/stm32cube_h7/README.md`
- Create: `stm32/firmware/third_party/FreeRTOS-Kernel/` (copied), `stm32/firmware/third_party/FreeRTOS-Kernel/README.md`
- Create: `stm32/firmware/tools/mkioc.py`, `stm32/firmware/tools/cubegen.sh`
- Create (generated): `stm32/firmware/cube/boot/**`, `stm32/firmware/cube/app/**`
- Create: `stm32/firmware/tools/check_pins.py`
- Test: `stm32/firmware/tools/test_check_pins.py`
- Modify: `stm32/.gitignore`

**Interfaces:**
- Produces:
  - **Generated tree:** `cube/boot/{boot.ioc,Inc/,Src/,startup_stm32h750xx.s,stm32h750vbtx_flash.ld}` and the same for `cube/app/`.
  - **Generated functions:** `MX_GPIO_Init`, `MX_IWDG1_Init` (boot), `MX_QUADSPI_Init`, `MX_UART7_Init`, `MX_TIM2_Init` (app).
  - **Handles:** `hqspi`, `huart7`, `hiwdg1`, `htim2`.
  - **Pin labels** in `main.h`: `WDI_LED`, `LED_NET`, `DO1_D`, `DO2_D`, `LATCH_LE`, `LATCH_OE`, `LTE_PWR`, `LTE_RESET`, `ETH_RESET`, `SPI4_NSS`, `RELOAD`, `DI1`, `DI2`.
  - **USER CODE blocks** used by Tasks 4 and 5: `main.c` `Includes`, `1`, `2`; `tim.c` `TIM2_Init 2`.

- [ ] **Step 1: Vendor the pinned HAL, CMSIS and FreeRTOS**

The sources are the GitHub releases already cloned in `~/opt/st`. The step checks each checkout's tag and commit before copying.

```bash
ST=~/opt/st
for p in "stm32h7xx-hal-driver-v1.11.6 v1.11.6 a1996eed9172b59887bafaaa0ea1816ea14d48b5" \
         "cmsis-device-h7-v1.10.7 v1.10.7 8f922cdc7cc6de2344e75ddd657889f4ff761790" \
         "cmsis-core-v5.9.0_20250520 v5.9.0_20250520 b7487de1303e2eb77c402432e368691e9dcfb5f0" \
         "FreeRTOS-Kernel-V11.3.1 V11.3.1 3a22924e0a9ddbbc8b0758881c33b3422a5cc20d"; do
  set -- $p; test "$(git -C $ST/$1 describe --tags) $(git -C $ST/$1 rev-parse HEAD)" = "$2 $3" && echo "pinned $1"
done
H=stm32/firmware/third_party/stm32cube_h7/Drivers
mkdir -p $H/STM32H7xx_HAL_Driver $H/CMSIS/Include $H/CMSIS/Device/ST/STM32H7xx/Include $H/CMSIS/Device/ST/STM32H7xx/Source/Templates
cp -r $ST/stm32h7xx-hal-driver-v1.11.6/Inc $ST/stm32h7xx-hal-driver-v1.11.6/Src $ST/stm32h7xx-hal-driver-v1.11.6/LICENSE.md $H/STM32H7xx_HAL_Driver/
cp $ST/cmsis-device-h7-v1.10.7/Include/{stm32h7xx.h,stm32h750xx.h,system_stm32h7xx.h} $H/CMSIS/Device/ST/STM32H7xx/Include/
cp $ST/cmsis-device-h7-v1.10.7/Source/Templates/system_stm32h7xx.c $H/CMSIS/Device/ST/STM32H7xx/Source/Templates/
cp $ST/cmsis-device-h7-v1.10.7/LICENSE.md $H/CMSIS/Device/ST/STM32H7xx/
cp $ST/cmsis-core-v5.9.0_20250520/Include/*.h $H/CMSIS/Include/
cp $ST/cmsis-core-v5.9.0_20250520/LICENSE.md $H/CMSIS/
R=stm32/firmware/third_party/FreeRTOS-Kernel
mkdir -p $R/portable/GCC $R/portable/MemMang
cp $ST/FreeRTOS-Kernel-V11.3.1/{tasks,queue,list,timers,event_groups,stream_buffer,croutine}.c $ST/FreeRTOS-Kernel-V11.3.1/LICENSE.md $R/
cp -r $ST/FreeRTOS-Kernel-V11.3.1/include $R/
cp -r $ST/FreeRTOS-Kernel-V11.3.1/portable/GCC/ARM_CM4F $R/portable/GCC/
cp $ST/FreeRTOS-Kernel-V11.3.1/portable/MemMang/heap_5.c $R/portable/MemMang/
grep -c "{0, 0, 0, 0, 1, 2, 3, 4, 1, 2, 3, 4, 6, 7, 8, 9}" $H/CMSIS/Device/ST/STM32H7xx/Source/Templates/system_stm32h7xx.c
```

Expected: four `pinned …` lines, then `1` (the fixed prescaler table, B3).

Create `stm32/firmware/third_party/stm32cube_h7/README.md`:

```markdown
# STM32H7 HAL and CMSIS (vendored, pinned)

| Part | Release | Commit |
|---|---|---|
| `Drivers/STM32H7xx_HAL_Driver` (Inc, Src) | STMicroelectronics/stm32h7xx-hal-driver v1.11.6 | a1996eed9172b59887bafaaa0ea1816ea14d48b5 |
| `Drivers/CMSIS/Device/ST/STM32H7xx` (stm32h7xx.h, stm32h750xx.h, system_stm32h7xx.h, Source/Templates/system_stm32h7xx.c) | STMicroelectronics/cmsis-device-h7 v1.10.7 | 8f922cdc7cc6de2344e75ddd657889f4ff761790 |
| `Drivers/CMSIS/Include` | STMicroelectronics/cmsis-core v5.9.0_20250520 | b7487de1303e2eb77c402432e368691e9dcfb5f0 |

Licences: BSD-3-Clause (HAL), Apache-2.0 (CMSIS); see the LICENSE.md next to each part.

Why not the copy CubeMX makes from FW pack 1.12.0: that pack's `system_stm32h7xx.c` has
`D1CorePrescTable = {0, 0, 0, 0, 0, 0, 0, 0, 1, ...}`, so the HAL reads an APB prescaler of 2 as 1 and every
peripheral clock and baud rate comes out twice too high. Both images build `Source/Templates/system_stm32h7xx.c`
from v1.10.7 instead of the generated one. CubeMX still writes `stm32h7xx_hal_conf.h` for each image.
```

Create `stm32/firmware/third_party/FreeRTOS-Kernel/README.md`:

```markdown
# FreeRTOS-Kernel V11.3.1 (vendored, pinned)

FreeRTOS/FreeRTOS-Kernel tag V11.3.1, commit 3a22924e0a9ddbbc8b0758881c33b3422a5cc20d. MIT licence (LICENSE.md).
Kept: the kernel sources, `include/`, `portable/GCC/ARM_CM4F` (the Cortex-M7 r1p1 of the STM32H7 uses the
CM4F port; the ARM_CM7 port is only for r0p1) and `portable/MemMang/heap_5.c` (Plan 05).
```

- [ ] **Step 2: Write the `.ioc` authoring script**

Create `stm32/firmware/tools/mkioc.py`:

```python
#!/usr/bin/env python3
"""How cube/boot/boot.ioc and cube/app/app.ioc were authored (spec §3, §6.2).

    python3 stm32/firmware/tools/mkioc.py boot stm32/firmware/cube/boot/boot.ioc
    python3 stm32/firmware/tools/mkioc.py app  stm32/firmware/cube/app/app.ioc

CubeMX canonicalises both files on the first tools/cubegen.sh run. From then on the committed .ioc
files are the source: change them in CubeMX (or by hand), then run cubegen.sh and check_pins.py.
This script records every non-default value and why it is there.
"""
import sys

kind, out = sys.argv[1], sys.argv[2]
assert kind in ('boot', 'app')
BOOT = kind == 'boot'

pins = []


def pin(p, **kv):
    pins.append((p, kv))


pin('PH0-OSC_IN\\ (PH0)', Mode='HSE-External-Oscillator', Signal='RCC_OSC_IN')
pin('PH1-OSC_OUT\\ (PH1)', Mode='HSE-External-Oscillator', Signal='RCC_OSC_OUT')
pin('PA13\\ (JTMS/SWDIO)', Mode='Serial_Wire', Signal='DEBUG_JTMS-SWDIO')
pin('PA14\\ (JTCK/SWCLK)', Mode='Serial_Wire', Signal='DEBUG_JTCK-SWCLK')
for p, s in [('PB2', 'QUADSPI_CLK'), ('PC11', 'QUADSPI_BK2_NCS'), ('PE7', 'QUADSPI_BK2_IO0'),
             ('PE8', 'QUADSPI_BK2_IO1'), ('PE9', 'QUADSPI_BK2_IO2'), ('PE10', 'QUADSPI_BK2_IO3')]:
    # 80 MHz needs the fastest slew; with CubeMX's default "Low" every QSPI read times out.
    pin(p, Mode='Single Bank 2', Signal=s, GPIO_Speed='GPIO_SPEED_FREQ_VERY_HIGH', GPIOParameters='GPIO_Speed')
pin('PB3\\ (JTDO/TRACESWO)', Mode='Asynchronous', Signal='UART7_RX')
pin('PB4\\ (NJTRST)', Mode='Asynchronous', Signal='UART7_TX')
if not BOOT:
    pin('PA9', Mode='Asynchronous', Signal='USART1_TX')
    pin('PA10', Mode='Asynchronous', Signal='USART1_RX')
    pin('PA12', Mode='Hardware Flow Control (RS485)', Signal='USART1_DE')
    pin('PD5', Mode='Asynchronous', Signal='USART2_TX')
    pin('PD6', Mode='Asynchronous', Signal='USART2_RX')
    pin('PD4', Mode='Hardware Flow Control (RS485)', Signal='USART2_DE')
    pin('PD8', Mode='Asynchronous', Signal='USART3_TX')
    pin('PD9', Mode='Asynchronous', Signal='USART3_RX')
    for p, s in [('PA1', 'ETH_REF_CLK'), ('PA2', 'ETH_MDIO'), ('PA7', 'ETH_CRS_DV'), ('PC1', 'ETH_MDC'),
                 ('PC4', 'ETH_RXD0'), ('PC5', 'ETH_RXD1'), ('PB11', 'ETH_TX_EN'), ('PB12', 'ETH_TXD0'),
                 ('PB13', 'ETH_TXD1')]:
        pin(p, Mode='RMII', Signal=s)
    pin('PE2', Mode='Full_Duplex_Master', Signal='SPI4_SCK')
    pin('PE5', Mode='Full_Duplex_Master', Signal='SPI4_MISO')
    pin('PE6', Mode='Full_Duplex_Master', Signal='SPI4_MOSI')
    pin('PC2_C', Mode='IN0-Single-Ended', Signal='ADC3_INP0')
    pin('PC3_C', Mode='IN1-Single-Ended', Signal='ADC3_INP1')


def out_pin(p, label, state):
    pin(p, Signal='GPIO_Output', GPIO_Label=label, PinState=state, Locked='true', GPIOParameters='PinState,GPIO_Label')


def in_pin(p, label, pupd=None):
    kv = dict(Signal='GPIO_Input', GPIO_Label=label, Locked='true', GPIOParameters='GPIO_Label')
    if pupd:
        kv.update(GPIO_PuPd=pupd, GPIOParameters='GPIO_PuPd,GPIO_Label')
    pin(p, **kv)


# Safe levels (spec §11.2 step 2): latch outputs disabled (OE high), D1/D2/LE low, modem PWRKEY and
# RESET_N released, PHY held in reset, SPI4 flash deselected (software NSS on PE4).
out_pin('PC10', 'WDI_LED', 'GPIO_PIN_RESET')
out_pin('PE11', 'LED_NET', 'GPIO_PIN_RESET')
out_pin('PD10', 'DO1_D', 'GPIO_PIN_RESET')
out_pin('PD11', 'DO2_D', 'GPIO_PIN_RESET')
out_pin('PD14', 'LATCH_LE', 'GPIO_PIN_RESET')
out_pin('PD15', 'LATCH_OE', 'GPIO_PIN_SET')
out_pin('PA5', 'LTE_PWR', 'GPIO_PIN_RESET')
out_pin('PA4', 'LTE_RESET', 'GPIO_PIN_RESET')
out_pin('PD13', 'ETH_RESET', 'GPIO_PIN_RESET')
out_pin('PE4', 'SPI4_NSS', 'GPIO_PIN_SET')
in_pin('PA8', 'RELOAD', 'GPIO_PULLUP')
in_pin('PB7', 'DI1')
in_pin('PB6', 'DI2')

vps = [('VP_SYS_VS_Systick', 'SysTick', 'SYS_VS_Systick'),
       ('VP_MEMORYMAP_VS_MEMORYMAP', 'CurAppReg', 'MEMORYMAP_VS_MEMORYMAP')]
if BOOT:
    vps.append(('VP_IWDG1_VS_IWDG', 'IWDG_Activate', 'IWDG1_VS_IWDG'))
else:
    vps += [('VP_TIM2_VS_ClockSourceINT', 'Internal', 'TIM2_VS_ClockSourceINT'),
            ('VP_TIM6_VS_ClockSourceINT', 'Enable_Timer', 'TIM6_VS_ClockSourceINT')]

ips = ['CORTEX_M7', 'DEBUG', 'MEMORYMAP', 'NVIC', 'QUADSPI', 'RCC', 'SYS', 'UART7']
ips += ['IWDG1'] if BOOT else ['ADC3', 'ETH', 'SPI4', 'TIM2', 'TIM6', 'USART1', 'USART2', 'USART3']
ips.sort()

L = []
a = L.append
a('#MicroXplorer Configuration settings - do not modify')
a('File.Version=6')
a('KeepUserPlacement=false')
a('Mcu.CPN=STM32H750VBT6')
a('Mcu.Family=STM32H7')
for i, ip in enumerate(ips):
    a('Mcu.IP%d=%s' % (i, ip))
a('Mcu.IPNb=%d' % len(ips))
a('Mcu.Name=STM32H750VBTx')
a('Mcu.Package=LQFP100')
allp = [p for p, _ in pins] + [v[0] for v in vps]
for i, p in enumerate(allp):
    a('Mcu.Pin%d=%s' % (i, p))
a('Mcu.PinsNb=%d' % len(allp))
a('Mcu.ThirdPartyNb=0')
a('Mcu.UserConstants=')
a('Mcu.UserName=STM32H750VBTx')
for p, kv in pins:
    for k in sorted(kv):
        a('%s.%s=%s' % (p, k, kv[k]))
for v, m, s in vps:
    a('%s.Mode=%s' % (v, m))
    a('%s.Signal=%s' % (v, s))


def ipp(ip, kv, order=None):
    # CubeMX applies parameters in IPParameters order and drops any whose condition is not yet true,
    # so the enabling parameters come first when given.
    keys = (order or []) + sorted(k for k in kv if k not in (order or []))
    for k in keys:
        a('%s.%s=%s' % (ip, k, kv[k]))
    a('%s.IPParameters=%s' % (ip, ','.join(keys)))


# PLL1 and the bus tree are the frozen part (bootloader, spec §6.2). PLL2 (ADC) is configured by the
# app's HAL_ADC_MspInit, so only app.ioc carries it.
rcc = dict(HSE_VALUE='25000000', PLLSourceVirtual='RCC_PLLSOURCE_HSE', SYSCLKSource='RCC_SYSCLKSOURCE_PLLCLK',
           DIVM1='5', DIVN1='192', DIVP1='2', DIVQ1='4', DIVR1='2',
           D1CPRE='RCC_SYSCLK_DIV1', HPRE='RCC_HCLK_DIV2', D1PPRE='RCC_APB3_DIV2', D2PPRE1='RCC_APB1_DIV2',
           D2PPRE2='RCC_APB2_DIV2', D3PPRE='RCC_APB4_DIV2',
           PWR_Regulator_Voltage_Scale='PWR_REGULATOR_VOLTAGE_SCALE0', SupplySource='PWR_LDO_SUPPLY',
           QSPICLockSelection='RCC_QSPICLKSOURCE_D1HCLK',
           USART234578CLockSelection='RCC_USART234578CLKSOURCE_D2PCLK1')
if not BOOT:
    rcc.update(DIVM2='5', DIVN2='80', DIVP2='20', DIVQ2='4', DIVR2='4', ADCCLockSelection='RCC_ADCCLKSOURCE_PLL2',
               Spi45ClockSelection='RCC_SPI45CLKSOURCE_D2PCLK1', USART16CLockSelection='RCC_USART16CLKSOURCE_D2PCLK2')
ipp('RCC', rcc)
# FlashSize 22 (8 MB) for a 4 MB part: ES0396 §2.4.4, a memory-mapped read of the last byte of the
# FSIZE region returns 0 and a repeated read stalls the AXI bus. MPU region 1 still ends at 4 MB.
ipp('QUADSPI', dict(ClockPrescaler='2', FlashSize='22', SampleShifting='QSPI_SAMPLE_SHIFTING_HALFCYCLE',
                    ChipSelectHighTime='QSPI_CS_HIGH_TIME_2_CYCLE', FifoThreshold='4', DualFlash='QSPI_DUALFLASH_DISABLE'))
ipp('UART7', dict(BaudRate='115200', VirtualMode='VM_ASYNC'))
if BOOT:
    # LSI 32 kHz / 32 = 1 kHz; 1600 ticks = 1.6 s nominal (1.5 to 1.74 s over the LSI range).
    ipp('IWDG1', dict(Prescaler='IWDG_PRESCALER_32', Reload='1600'))
    # Background region 0 (CubeMX default: 0x60000000-0xDFFFFFFF no access) plus the 4 MB QSPI window.
    # With default_mode_Activation=1 CubeMX uses the "_S" variant of every region parameter.
    r1 = 'Cortex_Memory_Protection_Unit_Region1_Settings_S'
    cm = {'MPU_Control': 'MPU_PRIVILEGED_DEFAULT', 'default_mode_Activation': '1',
          'Enable_S-' + r1: 'MPU_REGION_ENABLE', 'BaseAddress_S-' + r1: '0x90000000', 'Size_S-' + r1: 'MPU_REGION_SIZE_4MB',
          'SubRegionDisable_S-' + r1: '0x0', 'TypeExtField_S-' + r1: 'MPU_TEX_LEVEL0',
          'AccessPermission_S-' + r1: 'MPU_REGION_PRIV_RO_URO', 'DisableExec_S-' + r1: 'MPU_INSTRUCTION_ACCESS_ENABLE',
          'IsShareable_S-' + r1: 'MPU_ACCESS_NOT_SHAREABLE', 'IsCacheable_S-' + r1: 'MPU_ACCESS_CACHEABLE',
          'IsBufferable_S-' + r1: 'MPU_ACCESS_NOT_BUFFERABLE'}
    ipp('CORTEX_M7', cm, ['default_mode_Activation', 'MPU_Control', 'Enable_S-' + r1])
else:
    ipp('TIM2', dict(Prescaler='239', Period='4294967295'))  # 240 MHz / 240 = 1 MHz, 32-bit free run
    ipp('TIM6', dict(Prescaler='239', Period='999'))         # 1 kHz ADC trigger (Plan 07)

# NVIC flags: enabled:preempt:sub:?:?:generate-handler:?:call-HAL:? (field 6 generates the handler).
for irq in ['NonMaskableInt', 'HardFault', 'MemoryManagement', 'BusFault', 'UsageFault', 'DebugMonitor']:
    a('NVIC.%s_IRQn=true\\:0\\:0\\:false\\:false\\:true\\:false\\:false\\:false' % irq)
own = 'true' if BOOT else 'false'  # in the app, FreeRTOS owns SVC, PendSV and SysTick
for irq in ['SVCall', 'PendSV']:
    a('NVIC.%s_IRQn=true\\:0\\:0\\:false\\:false\\:%s\\:false\\:false\\:false' % (irq, own))
a('NVIC.SysTick_IRQn=true\\:15\\:0\\:false\\:false\\:%s\\:false\\:%s\\:false' % (own, own))
a('NVIC.PriorityGroup=NVIC_PRIORITYGROUP_4')
a('NVIC.ForceEnableDMAVector=true')
a('board=custom')


def fl(entries):
    return ','.join('%d-%s-%s-%s-HAL-%s' % e for e in entries)


if BOOT:
    funcs = [(1, 'SystemClock_Config', 'RCC', 'false', 'false'), (2, 'MX_GPIO_Init', 'GPIO', 'false', 'true'),
             (3, 'MX_IWDG1_Init', 'IWDG1', 'false', 'true'), (4, 'MX_QUADSPI_Init', 'QUADSPI', 'false', 'true'),
             (5, 'MX_UART7_Init', 'UART7', 'false', 'true'), (0, 'MX_CORTEX_M7_Init', 'CORTEX_M7', 'false', 'true')]
else:
    # "true" in the 4th field = do not generate the call: the clocks and QSPI belong to the bootloader,
    # the MPU to Reset_Handler; the peripherals of later plans stay uncalled until those plans.
    funcs = [(1, 'SystemClock_Config', 'RCC', 'true', 'false'), (2, 'MX_GPIO_Init', 'GPIO', 'false', 'true'),
             (3, 'MX_TIM2_Init', 'TIM2', 'false', 'true'), (4, 'MX_UART7_Init', 'UART7', 'false', 'true'),
             (5, 'MX_QUADSPI_Init', 'QUADSPI', 'true', 'true'), (6, 'MX_ADC3_Init', 'ADC3', 'true', 'true'),
             (7, 'MX_ETH_Init', 'ETH', 'true', 'true'), (8, 'MX_SPI4_Init', 'SPI4', 'true', 'true'),
             (9, 'MX_TIM6_Init', 'TIM6', 'true', 'true'), (10, 'MX_USART1_UART_Init', 'USART1', 'true', 'true'),
             (11, 'MX_USART2_UART_Init', 'USART2', 'true', 'true'), (12, 'MX_USART3_UART_Init', 'USART3', 'true', 'true'),
             (0, 'MX_CORTEX_M7_Init', 'CORTEX_M7', 'true', 'true')]
pm = dict(AskForMigrate='false', BackupPrevious='false', CompilerOptimize='6', ComputerToolchain='false',
          CoupleFile='true', CustomerFirmwarePackage='', DefaultFWLocation='true', DeletePrevious='true',
          DeviceId='STM32H750VBTx', FirmwarePackage='STM32Cube FW_H7 V1.12.0', FreePins='false',
          HalAssertFull='false', HeapSize='0x0', KeepUserCode='true', LastFirmware='false', LibraryCopy='1',
          MainLocation='Src', NoMain='false', PreviousToolchain='', ProjectBuild='false',
          ProjectFileName=kind + '.ioc', ProjectName=kind, ProjectStructure='', RegisterCallBack='',
          StackSize='0x1000', TargetToolchain='CMake', ToolChainLocation='', UAScriptAfterPath='',
          UAScriptBeforePath='', UnderRoot='false', functionlistsort=fl(funcs))
for k in sorted(pm):
    a('ProjectManager.%s=%s' % (k, pm[k]))
open(out, 'w').write('\n'.join(L) + '\n')
```

`FirmwarePackage` is V1.12.0, the pack in the CubeMX repository. V1.12.1, CubeMX's default, needs a my.st.com login to download, and generation then fails with KO.

- [ ] **Step 3: Write the generation script**

Create `stm32/firmware/tools/cubegen.sh` (mode 755):

```bash
#!/bin/bash
# Regenerates cube/boot and cube/app from their .ioc with STM32CubeMX in headless mode (spec §4:
# CubeMX only generates code; our edits live in USER CODE blocks, which CubeMX keeps).
#   stm32/firmware/tools/cubegen.sh
# CUBEMX is the install dir (default ~/opt/stm32cubemx), CUBEMX_JAVA a Java 17+ runtime.
set -euo pipefail
FW=$(cd "$(dirname "$0")/.." && pwd)
CUBEMX=${CUBEMX:-$HOME/opt/stm32cubemx}
JAVA=${CUBEMX_JAVA:-$HOME/.local/opt/android-studio/jbr/bin/java}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

generate() {  # $1 = boot|app
    printf 'config load %s\nproject name %s\nproject toolchain CMake\nproject path %s\nproject generate\nexit\n' \
        "$FW/cube/$1/$1.ioc" "$1" "$FW/cube" > "$work/$1.txt"
    (cd "$CUBEMX" && "$JAVA" -jar STM32CubeMX.exe -q "$work/$1.txt") > "$work/$1.log" 2>&1 || true
    if [ "$(grep -c '^OK' "$work/$1.log")" -ne 5 ] || grep -q '^KO' "$work/$1.log"; then
        grep -B3 '^KO' "$work/$1.log" || tail -20 "$work/$1.log"
        echo "cubegen: $1 FAILED"
        exit 1
    fi
}

for k in boot app; do
    d="$FW/cube/$k"
    # A fresh .ioc may get its toolchain files under Projects/ on the first generation; generation is
    # idempotent once they sit next to Src/ and ToolChainLocation is empty (Plan 02, B12).
    for try in 1 2 3 4; do
        generate "$k"
        if [ -f "$d/startup_stm32h750xx.s" ] && [ -f "$d/Src/syscalls.c" ] && grep -q '^ProjectManager.ToolChainLocation=$' "$d/$k.ioc"; then
            break
        fi
        [ "$try" -lt 4 ] || { echo "cubegen: $k toolchain files not placed next to Src/"; exit 1; }
        rm -rf "$d/Projects"
        sed -i 's/^ProjectManager.ToolChainLocation=.*/ProjectManager.ToolChainLocation=/' "$d/$k.ioc"
    done
    # Both images build against the one HAL in third_party/stm32cube_h7; CubeMX's own CMake project is unused.
    rm -rf "$d/Drivers" "$d/CMakeLists.txt" "$d/CMakePresets.json" "$d/cmake"
    echo "cubegen: $k ok"
done
```

- [ ] **Step 4: Generate both projects and prove the generation is idempotent**

```bash
mkdir -p stm32/firmware/cube/boot stm32/firmware/cube/app
python3 stm32/firmware/tools/mkioc.py boot stm32/firmware/cube/boot/boot.ioc
python3 stm32/firmware/tools/mkioc.py app stm32/firmware/cube/app/app.ioc
chmod +x stm32/firmware/tools/cubegen.sh && stm32/firmware/tools/cubegen.sh
cp -r stm32/firmware/cube /tmp/claude-1000/cube_first
stm32/firmware/tools/cubegen.sh && diff -rq /tmp/claude-1000/cube_first stm32/firmware/cube && echo IDEMPOTENT
rm -rf /tmp/claude-1000/cube_first
ls stm32/firmware/cube/boot stm32/firmware/cube/boot/Src stm32/firmware/cube/app/Src
```

Expected:
- `cubegen: boot ok` and `cubegen: app ok` twice, then `IDEMPOTENT`.
- `cube/boot/` holds `boot.ioc Inc Src startup_stm32h750xx.s stm32h750vbtx_flash.ld`.
- `cube/boot/Src` holds `gpio.c iwdg.c main.c memorymap.c quadspi.c stm32h7xx_hal_msp.c stm32h7xx_it.c syscalls.c sysmem.c system_stm32h7xx.c usart.c`.
- `cube/app/Src` holds, in addition, `adc.c eth.c spi.c tim.c`, and no `iwdg.c`.

Then confirm what the generated code does:

```bash
F=stm32/firmware/cube
sed -n '/int main(void)/,/while (1)/p' $F/boot/Src/main.c | grep -E '^\s+(MX_|MPU_|HAL_Init|SystemClock)'
sed -n '/int main(void)/,/while (1)/p' $F/app/Src/main.c | grep -E '^\s+(MX_|MPU_|HAL_Init|SystemClock)'
grep -o 'void [A-Za-z]*_Handler' $F/app/Src/stm32h7xx_it.c | tr '\n' ' '; echo
grep -E 'FlashSize|FlashID|Speed' $F/boot/Src/quadspi.c
sed -n '/MPU_REGION_NUMBER1/,/HAL_MPU_ConfigRegion/p' $F/boot/Src/main.c | grep -E 'BaseAddress|Size =|AccessPermission'
grep -E 'PWR_LDO_SUPPLY|VOLTAGE_SCALE0|FLASH_LATENCY_4' $F/boot/Src/main.c
```

Expected:
- **boot `main()`:** `MPU_Config(); HAL_Init(); SystemClock_Config(); MX_GPIO_Init(); MX_IWDG1_Init(); MX_QUADSPI_Init(); MX_UART7_Init();`.
- **app `main()`:** `HAL_Init(); MX_GPIO_Init(); MX_TIM2_Init(); MX_UART7_Init();` and nothing else.
- **App handlers:** only `NMI_Handler HardFault_Handler MemManage_Handler BusFault_Handler UsageFault_Handler DebugMon_Handler`, so no SVC, PendSV or SysTick.
- **QSPI:** `FlashSize = 22`, `FlashID = QSPI_FLASH_ID_2`, and three `GPIO_SPEED_FREQ_VERY_HIGH` lines.
- **MPU region 1:** `0x90000000`, `MPU_REGION_SIZE_4MB`, `MPU_REGION_PRIV_RO_URO`.
- **Power and flash:** one line each for `PWR_LDO_SUPPLY`, `PWR_REGULATOR_VOLTAGE_SCALE0` and `FLASH_LATENCY_4`.

- [ ] **Step 5: Write the pin-gate test first**

Create `stm32/firmware/tools/test_check_pins.py`:

```python
#!/usr/bin/env python3
"""Proves that check_pins.py fails on each kind of mistake it exists for.

    python3 stm32/firmware/tools/test_check_pins.py
"""
import contextlib
import io
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_pins  # noqa: E402

FW = os.path.dirname(HERE)
BOOT = os.path.join(FW, 'cube/boot/boot.ioc')
APP = os.path.join(FW, 'cube/app/app.ioc')
DB = os.path.expanduser('~/opt/stm32cubemx/db/mcu')


def run(boot, app, db=DB):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = check_pins.main(['check_pins.py', '--db', db, boot, app])
    return rc, out.getvalue()


def mutate(src, dst, edits):
    text = open(src, encoding='latin-1').read()
    for old, new in edits:
        assert re.search(old, text, re.M), old
        text = re.sub(old, new, text, count=1, flags=re.M)
    open(dst, 'w', encoding='latin-1').write(text)


def main():
    failures = 0
    tmp = tempfile.mkdtemp()
    try:
        rc, out = run(BOOT, APP)
        if rc != 0:
            print('FAIL unmodified files rejected:\n' + out)
            failures += 1
        cases = [
            ('UART7 RX/TX swapped', APP,
             [(r'^PB3\\ \(JTDO/TRACESWO\)\.Signal=UART7_RX', r'PB3\\ (JTDO/TRACESWO).Signal=UART7_TX'),
              (r'^PB4\\ \(NJTRST\)\.Signal=UART7_TX', r'PB4\\ (NJTRST).Signal=UART7_RX')], 'PB3 carries UART7_TX'),
            ('PLL1 N differs from the bootloader', APP, [(r'^RCC\.DIVN1=192', 'RCC.DIVN1=180')], 'RCC.DIVN1 differs'),
            ('latch OE enabled at reset', BOOT, [(r'^PD15\.PinState=GPIO_PIN_SET', 'PD15.PinState=GPIO_PIN_RESET')],
             'PD15 initial level'),
            ('FSIZE without the ES0396 margin', BOOT, [(r'^QUADSPI\.FlashSize=22', 'QUADSPI.FlashSize=21')],
             'QUADSPI.FlashSize'),
            ('QSPI pin at low speed', APP,
             [(r'^PE9\.GPIO_Speed=GPIO_SPEED_FREQ_VERY_HIGH', 'PE9.GPIO_Speed=GPIO_SPEED_FREQ_LOW')],
             'PE9 is not at very high speed'),
            ('a pin spec §3 does not assign', APP, [(r'^(PA8\.Signal=GPIO_Input)', r'\1\nPB14.Signal=GPIO_Output')],
             'PB14 carries GPIO_Output'),
        ]
        for what, src, edits, expect in cases:
            boot, app = BOOT, APP
            dst = os.path.join(tmp, os.path.basename(src))
            mutate(src, dst, edits)
            if src == BOOT:
                boot = dst
            else:
                app = dst
            rc, out = run(boot, app)
            ok = rc == 1 and expect in out
            print(('ok   ' if ok else 'FAIL ') + what)
            failures += not ok
            os.remove(dst)
        # A signal the silicon does not have on that pin: drop QUADSPI_CLK from PB2 in a copy of the database.
        fake = os.path.join(tmp, 'db')
        os.mkdir(fake)
        name = 'STM32H750VBTx.xml'
        xml = open(os.path.join(DB, name), encoding='utf-8').read()
        i = xml.index('<Pin Name="PB2"')
        j = xml.index('</Pin>', i)
        open(os.path.join(fake, name), 'w', encoding='utf-8').write(
            xml[:i] + xml[i:j].replace('<Signal Name="QUADSPI_CLK"/>', '') + xml[j:])
        rc, out = run(BOOT, APP, fake)
        ok = rc == 1 and 'QUADSPI_CLK is not a signal of PB2' in out
        print(('ok   ' if ok else 'FAIL ') + 'signal missing from the CubeMX database')
        failures += not ok
    finally:
        shutil.rmtree(tmp)
    print('test_check_pins: %s' % ('ok' if not failures else 'FAILED'))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
```

- [ ] **Step 6: Run it to see it fail**

Run: `python3 stm32/firmware/tools/test_check_pins.py`
Expected: `ModuleNotFoundError: No module named 'check_pins'`.

- [ ] **Step 7: Write the pin gate**

Create `stm32/firmware/tools/check_pins.py`:

```python
#!/usr/bin/env python3
"""Pin and clock gate for cube/boot/boot.ioc and cube/app/app.ioc (spec §3, §6.2).

    python3 stm32/firmware/tools/check_pins.py [--db DIR] [BOOT_IOC APP_IOC]

Checks both files:
- every pin of the spec §3 table carries its signal, GPIO label and initial level;
- no other pin is assigned;
- every signal exists on its pin in the CubeMX MCU database;
- what the bootloader freezes (PLL1, bus prescalers, supply, QSPI, UART7) is identical in the two files.
Prints "pins: ok" or the failures and exits 1.
"""
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)

# spec §3: pin -> signal. The pins both images configure, then the app-only peripherals.
COMMON = {
    'PH0': 'RCC_OSC_IN', 'PH1': 'RCC_OSC_OUT', 'PA13': 'DEBUG_JTMS-SWDIO', 'PA14': 'DEBUG_JTCK-SWCLK',
    'PB2': 'QUADSPI_CLK', 'PC11': 'QUADSPI_BK2_NCS', 'PE7': 'QUADSPI_BK2_IO0', 'PE8': 'QUADSPI_BK2_IO1',
    'PE9': 'QUADSPI_BK2_IO2', 'PE10': 'QUADSPI_BK2_IO3',
    'PB3': 'UART7_RX', 'PB4': 'UART7_TX',
    'PC10': 'GPIO_Output', 'PE11': 'GPIO_Output', 'PD10': 'GPIO_Output', 'PD11': 'GPIO_Output',
    'PD14': 'GPIO_Output', 'PD15': 'GPIO_Output', 'PA5': 'GPIO_Output', 'PA4': 'GPIO_Output',
    'PD13': 'GPIO_Output', 'PE4': 'GPIO_Output', 'PA8': 'GPIO_Input', 'PB7': 'GPIO_Input', 'PB6': 'GPIO_Input',
}
APP_ONLY = {
    'PA9': 'USART1_TX', 'PA10': 'USART1_RX', 'PA12': 'USART1_DE',
    'PD5': 'USART2_TX', 'PD6': 'USART2_RX', 'PD4': 'USART2_DE', 'PD8': 'USART3_TX', 'PD9': 'USART3_RX',
    'PA1': 'ETH_REF_CLK', 'PA2': 'ETH_MDIO', 'PA7': 'ETH_CRS_DV', 'PC1': 'ETH_MDC', 'PC4': 'ETH_RXD0',
    'PC5': 'ETH_RXD1', 'PB11': 'ETH_TX_EN', 'PB12': 'ETH_TXD0', 'PB13': 'ETH_TXD1',
    'PE2': 'SPI4_SCK', 'PE5': 'SPI4_MISO', 'PE6': 'SPI4_MOSI',
    'PC2_C': 'ADC3_INP0', 'PC3_C': 'ADC3_INP1',
}
# GPIO label and level after MX_GPIO_Init: the safe state of spec §11.2 step 2.
GPIO = {
    'PC10': ('WDI_LED', 'GPIO_PIN_RESET'), 'PE11': ('LED_NET', 'GPIO_PIN_RESET'),
    'PD10': ('DO1_D', 'GPIO_PIN_RESET'), 'PD11': ('DO2_D', 'GPIO_PIN_RESET'),
    'PD14': ('LATCH_LE', 'GPIO_PIN_RESET'), 'PD15': ('LATCH_OE', 'GPIO_PIN_SET'),
    'PA5': ('LTE_PWR', 'GPIO_PIN_RESET'), 'PA4': ('LTE_RESET', 'GPIO_PIN_RESET'),
    'PD13': ('ETH_RESET', 'GPIO_PIN_RESET'), 'PE4': ('SPI4_NSS', 'GPIO_PIN_SET'),
    'PA8': ('RELOAD', None), 'PB7': ('DI1', None), 'PB6': ('DI2', None),
}
QSPI_PINS = ['PB2', 'PC11', 'PE7', 'PE8', 'PE9', 'PE10']
# What the bootloader sets up once and the app relies on (spec §6.2): identical in both files.
FROZEN = re.compile(r'^(RCC\.(HSE_VALUE|DIV[MNPQR]1|SYSCLKSource|PLLSourceVirtual|D1CPRE|HPRE|D[123]PPRE[12]?|'
                    r'PWR_Regulator_Voltage_Scale|SupplySource|QSPICLockSelection|SYSCLKFreq_VALUE|HCLKFreq_Value|'
                    r'APB[1234]Freq_Value|QSPIFreq_Value|FLatency)|QUADSPI\..*|UART7\..*)$')
EXPECT_KEYS = {
    'RCC.SYSCLKFreq_VALUE': '480000000', 'RCC.HCLKFreq_Value': '240000000', 'RCC.APB1Freq_Value': '120000000',
    'RCC.QSPIFreq_Value': '240000000', 'QUADSPI.ClockPrescaler': '2', 'QUADSPI.FlashSize': '22',
    'RCC.PWR_Regulator_Voltage_Scale': 'PWR_REGULATOR_VOLTAGE_SCALE0', 'RCC.SupplySource': 'PWR_LDO_SUPPLY',
}


def read_ioc(path):
    keys = {}
    for line in open(path, encoding='latin-1'):
        line = line.rstrip('\n')
        if line.startswith('#') or '=' not in line:
            continue
        k, v = line.split('=', 1)
        keys[k.replace('\\ ', ' ')] = v
    return keys


def short(pin):
    """'PH0-OSC_IN (PH0)' -> 'PH0', 'PA13 (JTMS/SWDIO)' -> 'PA13'."""
    return re.split(r'[- ]', pin, 1)[0]


def db_signals(db, mcu):
    out = {}
    for p in ET.parse(os.path.join(db, mcu + '.xml')).getroot().iter():
        if p.tag.endswith('Pin') and p.get('Name'):
            out[short(p.get('Name'))] = {s.get('Name') for s in p if s.tag.endswith('Signal')}
    return out


def check_file(name, keys, expected, db, fail):
    assigned = {short(k[:-len('.Signal')]): v for k, v in keys.items() if k.endswith('.Signal') and k.startswith('P')}
    for pin, sig in expected.items():
        if assigned.get(pin) != sig:
            fail('%s: %s carries %s, expected %s' % (name, pin, assigned.get(pin), sig))
    for pin, sig in assigned.items():
        if pin not in expected:
            fail('%s: %s carries %s, which spec §3 does not assign' % (name, pin, sig))
        elif not sig.startswith('GPIO_') and sig not in db.get(pin, set()):
            fail('%s: %s is not a signal of %s in the CubeMX database' % (name, sig, pin))
    full = {short(k.split('.')[0]): k.split('.')[0] for k in keys if k.startswith('P')}
    for pin, (label, level) in GPIO.items():
        k = full.get(pin, pin)
        if keys.get(k + '.GPIO_Label') != label:
            fail('%s: %s label %s, expected %s' % (name, pin, keys.get(k + '.GPIO_Label'), label))
        if level and keys.get(k + '.PinState', 'GPIO_PIN_RESET') != level:
            fail('%s: %s initial level %s, expected %s' % (name, pin, keys.get(k + '.PinState'), level))
    for pin in QSPI_PINS:
        if keys.get(full.get(pin, pin) + '.GPIO_Speed') != 'GPIO_SPEED_FREQ_VERY_HIGH':
            fail('%s: %s is not at very high speed (QSPI at 80 MHz)' % (name, pin))
    for k, v in EXPECT_KEYS.items():
        if keys.get(k) != v:
            fail('%s: %s = %s, expected %s' % (name, k, keys.get(k), v))


def main(argv):
    db = os.path.expanduser('~/opt/stm32cubemx/db/mcu')
    if len(argv) > 1 and argv[1] == '--db':
        db, argv = argv[2], argv[:1] + argv[3:]
    boot_path = argv[1] if len(argv) > 2 else os.path.join(FW, 'cube/boot/boot.ioc')
    app_path = argv[2] if len(argv) > 2 else os.path.join(FW, 'cube/app/app.ioc')
    failures = []
    fail = failures.append
    boot, app = read_ioc(boot_path), read_ioc(app_path)
    sig = db_signals(db, boot['Mcu.Name'])
    check_file('boot.ioc', boot, COMMON, sig, fail)
    check_file('app.ioc', app, {**COMMON, **APP_ONLY}, sig, fail)
    for k in sorted(set(boot) | set(app)):
        if FROZEN.match(k) and boot.get(k) != app.get(k):
            fail('%s differs: boot.ioc %s, app.ioc %s' % (k, boot.get(k), app.get(k)))
    for f in failures:
        print('FAIL ' + f)
    print('pins: ok' if not failures else 'pins: %d failure(s)' % len(failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
```

- [ ] **Step 8: Run the gate and its test**

Run: `python3 stm32/firmware/tools/check_pins.py && python3 stm32/firmware/tools/test_check_pins.py`
Expected: `pins: ok`, then seven `ok` lines and `test_check_pins: ok`.

- [ ] **Step 9: Ignore what CubeMX would add back, and commit**

Append to `stm32/.gitignore`:

```
# CubeMX output that the build does not use (tools/cubegen.sh deletes it after each generation)
firmware/cube/*/Drivers
firmware/cube/*/cmake
firmware/cube/*/CMakeLists.txt
firmware/cube/*/CMakePresets.json
firmware/cube/*/Projects
firmware/tools/__pycache__
```

```bash
git add stm32/.gitignore stm32/firmware/third_party/stm32cube_h7 stm32/firmware/third_party/FreeRTOS-Kernel \
        stm32/firmware/tools/mkioc.py stm32/firmware/tools/cubegen.sh stm32/firmware/tools/check_pins.py \
        stm32/firmware/tools/test_check_pins.py stm32/firmware/cube
git commit -m "stm32 cube: boot.ioc and app.ioc generated headless, pinned HAL/CMSIS/FreeRTOS, pin and clock gate"
```

### Task 3: Hardware-free parts with host tests (`tick64`, `crc32`, jump check)

**Files:**
- Create: `stm32/firmware/components/board/include/tick64.h`, `stm32/firmware/components/board/tick64.c`
- Create: `stm32/firmware/components/board/include/crc32.h`, `stm32/firmware/components/board/crc32.c`
- Create: `stm32/firmware/boot/jumpcheck.h`, `stm32/firmware/boot/jumpcheck.c`
- Test: `stm32/firmware/test/host/test_board.c`
- Modify: `stm32/firmware/test/host/Makefile`

**Interfaces:**
- Produces:
  - **tick64:** `typedef struct { uint32_t last; uint32_t high; } tick64_t;` and `uint64_t tick64_update(tick64_t *t, uint32_t raw);`.
  - **crc32:** `uint32_t crc32_update(uint32_t crc, const void *data, size_t len);` (zlib CRC-32; start with 0).
  - **Jump check:** `bool boot_app_ok(uint32_t msp, uint32_t reset);` and the macros `APP_BASE` 0x90000000, `APP_VECTORS` 0x90000400, `APP_SLOT_SIZE` 0x00170000.

- [ ] **Step 1: Write the headers and the failing test**

Create `stm32/firmware/components/board/include/tick64.h`:

```c
// 64-bit microsecond clock from a free-running 32-bit counter (spec §4: now_us()).
// Hardware-free so it runs in the host tests; components/board/now_us.c feeds it TIM2.
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t last;  // raw counter value at the previous update
    uint32_t high;  // number of counter wraps seen
} tick64_t;

// Extends raw to 64 bits. Correct as long as it is called at least once per counter period
// (2^32 us = 71.6 min at 1 MHz) and never concurrently with itself.
uint64_t tick64_update(tick64_t *t, uint32_t raw);
```

Create `stm32/firmware/components/board/include/crc32.h`:

```c
// CRC-32 (IEEE 802.3, reflected, init and final XOR 0xFFFFFFFF): the zlib/binascii crc32.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Continues a CRC: start with crc32_update(0, ...), pass the previous result to continue.
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
```

Create `stm32/firmware/boot/jumpcheck.h`:

```c
// The two checks before the bootloader jumps (spec §11.1, last bullets). The image header checks
// (magic, CRC, hardware id, size, SHA-256) come with mkimage.py in Plan 04; until then the reset
// vector is bounded by the whole slot.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define APP_BASE 0x90000000u
#define APP_VECTORS 0x90000400u    // header 1 KB, then the vector table
#define APP_SLOT_SIZE 0x00170000u  // 1 472 KB, header included (spec §5.1)

// msp and reset are the first two words of the vector table at APP_VECTORS.
bool boot_app_ok(uint32_t msp, uint32_t reset);
```

Create `stm32/firmware/test/host/test_board.c`:

```c
// Host tests for the hardware-free parts of board and boot: make -C stm32/firmware/test/host
#include <stdio.h>
#include "crc32.h"
#include "jumpcheck.h"
#include "tick64.h"

static int failures;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

static void test_tick64(void)
{
    tick64_t t = {0};
    CHECK(tick64_update(&t, 0) == 0, "start");
    CHECK(tick64_update(&t, 1000) == 1000, "plain step");
    CHECK(tick64_update(&t, 1000) == 1000, "same raw value: same time");
    CHECK(tick64_update(&t, 0xFFFFFFF0u) == 0xFFFFFFF0u, "just before the wrap");
    CHECK(tick64_update(&t, 0x10) == 0x100000010ull, "across the wrap");
    CHECK(tick64_update(&t, 0xFFFFFFFFu) == 0x1FFFFFFFFull, "last value of the second period");
    CHECK(tick64_update(&t, 0) == 0x200000000ull, "wrap to exactly 0");
    // Calls 200 ms apart (the wdog period) over 3 counter periods: exact and strictly increasing.
    tick64_t u = {0};
    uint64_t prev = 0, truth = 0;
    for (int i = 0; i < 3 * 21475; i++) {
        truth += 200000;
        uint64_t v = tick64_update(&u, (uint32_t)truth);
        CHECK(v == truth && v > prev, "step %d: %llu != %llu", i, (unsigned long long)v, (unsigned long long)truth);
        if (v != truth)
            break;
        prev = v;
    }
}

static void test_crc32(void)
{
    CHECK(crc32_update(0, "123456789", 9) == 0xCBF43926u, "check value of CRC-32/ISO-HDLC");
    CHECK(crc32_update(0, "", 0) == 0, "empty");
    uint32_t c = crc32_update(0, "1234", 4);
    CHECK(crc32_update(c, "56789", 5) == 0xCBF43926u, "continued over two calls");
}

static void test_jumpcheck(void)
{
    CHECK(boot_app_ok(0x20020000u, 0x900051E9u), "MSP at the top of DTCM, Thumb reset vector in the slot");
    CHECK(boot_app_ok(0x24080000u, 0x90000401u), "MSP at the top of AXI, reset at the vector table");
    CHECK(boot_app_ok(0x20000008u, 0x9016FFFFu), "lowest MSP, last address of the slot");
    CHECK(!boot_app_ok(0x20000000u, 0x900051E9u), "MSP at the DTCM base: empty stack");
    CHECK(!boot_app_ok(0x20020008u, 0x900051E9u), "MSP above DTCM");
    CHECK(!boot_app_ok(0x2001FFFCu, 0x900051E9u), "MSP not 8-byte aligned");
    CHECK(!boot_app_ok(0x30000000u, 0x900051E9u), "MSP in SRAM1");
    CHECK(!boot_app_ok(0x20020000u, 0x900051E8u), "reset vector without the Thumb bit");
    CHECK(!boot_app_ok(0x20020000u, 0x900003E1u), "reset vector inside the header");
    CHECK(!boot_app_ok(0x20020000u, 0x90170001u), "reset vector past the slot");
    CHECK(!boot_app_ok(0xFFFFFFFFu, 0xFFFFFFFFu), "erased flash");
    // What the bench QSPI held at 0x90000400 before Plan 02 (the old firmware, vectors at 0x90000000).
    CHECK(!boot_app_ok(0x8809F380u, 0xF380E001u), "old firmware's code read as vectors");
}

int main(void)
{
    test_tick64();
    test_crc32();
    test_jumpcheck();
    printf("test_board: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
```

In `stm32/firmware/test/host/Makefile`:
- add `-I../../components/board/include -I../../boot` to `INC`;
- add `$(BUILD)/test_board` to the `all` prerequisites;
- add `$(BUILD)/test_board` as the last command of `all`;
- add the rule:

```make
$(BUILD)/test_board: test_board.c ../../components/board/tick64.c ../../components/board/crc32.c ../../boot/jumpcheck.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@
```

- [ ] **Step 2: Run it to see it fail**

Run: `make -C stm32/firmware/test/host build/test_board`
Expected: make stops with `No rule to make target '../../components/board/tick64.c'`.

- [ ] **Step 3: Write the implementations**

Create `stm32/firmware/components/board/tick64.c`:

```c
#include "tick64.h"

uint64_t tick64_update(tick64_t *t, uint32_t raw)
{
    if (raw < t->last)
        t->high++;
    t->last = raw;
    return ((uint64_t)t->high << 32) | raw;
}
```

Create `stm32/firmware/components/board/crc32.c`:

```c
#include "crc32.h"

uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
```

Create `stm32/firmware/boot/jumpcheck.c`:

```c
#include "jumpcheck.h"

// An initial stack pointer is the end of the stack: above the region base, at most its end.
static bool in_ram(uint32_t sp, uint32_t base, uint32_t size)
{
    return sp > base && sp - base <= size;
}

bool boot_app_ok(uint32_t msp, uint32_t reset)
{
    bool msp_ok = (msp & 7u) == 0 && (in_ram(msp, 0x20000000u, 128u * 1024) || in_ram(msp, 0x24000000u, 512u * 1024));
    uint32_t pc = reset & ~1u;
    bool pc_ok = (reset & 1u) && pc >= APP_VECTORS && pc < APP_BASE + APP_SLOT_SIZE;
    return msp_ok && pc_ok;
}
```

- [ ] **Step 4: Run the whole host suite**

Run: `make -C stm32/firmware/test/host > /tmp/claude-1000/host.log 2>&1; echo rc=$?; grep -E "FAIL|: ok|FAILED" /tmp/claude-1000/host.log`
Expected: `rc=0`, with `test_board: ok` among the results of the 7 existing suites.

- [ ] **Step 5: Check that the tests bite (mutants, reverted after each)**

Each command applies one mutant, runs the suite and restores the file. Every one must print `test_board: FAILED`.

```bash
M() { cp "$1" /tmp/claude-1000/m.bak; sed -i "$2" "$1"; make -C stm32/firmware/test/host build/test_board >/dev/null 2>&1 && stm32/firmware/test/host/build/test_board | tail -1; cp /tmp/claude-1000/m.bak "$1"; }
M stm32/firmware/components/board/tick64.c 's/raw < t->last/raw <= t->last/'
M stm32/firmware/boot/jumpcheck.c 's/sp > base \&\& sp - base <= size/sp >= base \&\& sp - base <= size/'
M stm32/firmware/boot/jumpcheck.c 's/pc >= APP_VECTORS/pc >= APP_BASE/'
M stm32/firmware/components/board/crc32.c 's/return ~crc;/return crc;/'
make -C stm32/firmware/test/host build/test_board >/dev/null && stm32/firmware/test/host/build/test_board
```

Expected: four `test_board: FAILED` lines, then `test_board: ok`.

- [ ] **Step 6: Commit**

```bash
git add stm32/firmware/components/board stm32/firmware/boot/jumpcheck.c stm32/firmware/boot/jumpcheck.h \
        stm32/firmware/test/host/test_board.c stm32/firmware/test/host/Makefile
git commit -m "stm32 board: tick64 (now_us extension), crc32, bootloader jump check, with host tests"
```

### Task 4: Cross build, bootloader stub, flashing and the first boot

**Files:**
- Create: `stm32/firmware/cmake/arm-none-eabi.cmake`, `stm32/firmware/CMakePresets.json`, `stm32/firmware/CMakeLists.txt`
- Create: `stm32/firmware/components/board/include/board.h`, `stm32/firmware/components/board/board_io.c`
- Create: `stm32/firmware/boot/qspi_flash.h`, `stm32/firmware/boot/qspi_flash.c`, `stm32/firmware/boot/boot.h`, `stm32/firmware/boot/boot.c`
- Modify: `stm32/firmware/cube/boot/Src/main.c` (USER CODE `Includes`, `1`, `2` only)
- Create: `stm32/firmware/tools/flash.sh`, `stm32/firmware/test/bench/board.py`

**Interfaces:**
- Consumes: Task 2's generated `cube/boot` (the handles `hqspi`, `huart7` and `hiwdg1`, `MX_GPIO_Init`, the pin labels); Task 3's `boot_app_ok`, `APP_VECTORS`.
- Produces:
  - **Board:** `void board_kick(void);` and `void board_latch_zero(void);`.
  - **QSPI driver:** `int qf_init(QSPI_HandleTypeDef *h, qf_kick_fn kick);`, `uint32_t qf_read_jedec(QSPI_HandleTypeDef *h);`, `int qf_memory_mapped(QSPI_HandleTypeDef *h);`, and the codes `QF_OK 0`, `QF_EHAL -1`, `QF_EID -2`, `QF_EQE -3`, `QF_ETIMEOUT -4`.
  - **Bootloader:** `void boot_early(void);`, `void boot_main(void);`.
  - **Build:** `build/h750/boot.elf` and `boot.bin`, from the CMake target `boot`.
  - **Flashing:** `tools/flash.sh boot|app|both|blank [BUILD_DIR]`.
  - **Bench:** `board.listen(seconds, sends=(), during=None) -> [(ms, line)]` and `board.hard_reset()`.
  - **Boot log lines** later tasks parse: `SBIOT-H750 boot 0.2.0 rsr=XXXXXXXX sysclk=480000000`, `boot: qspi id c84016`, `boot: jump msp XXXXXXXX pc XXXXXXXX`, `boot: no valid app (msp …, reset …)`.

- [ ] **Step 1: Toolchain file and presets**

Create `stm32/firmware/cmake/arm-none-eabi.cmake`:

```cmake
# Cross toolchain: xPack arm-none-eabi-gcc 15.2.1 on PATH (source ~/opt/xpack/env.sh).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

Create `stm32/firmware/CMakePresets.json`:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "h750",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/h750",
      "toolchainFile": "${sourceDir}/cmake/arm-none-eabi.cmake",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo" }
    }
  ],
  "buildPresets": [
    { "name": "boot", "configurePreset": "h750", "targets": ["boot"] },
    { "name": "app", "configurePreset": "h750", "targets": ["app"] }
  ]
}
```

- [ ] **Step 2: The CMake tree with the bootloader target**

Create `stm32/firmware/CMakeLists.txt` (Task 5 appends the app target at its end):

```cmake
# SBIOT-H750 firmware images (spec §4, §12.4): boot.elf (internal flash) and app.elf/app.bin (QSPI XIP).
#   cmake --preset h750 && cmake --build --preset boot && cmake --build --preset app
cmake_minimum_required(VERSION 3.22)
project(sbiot_h750 C ASM)

set(CUBE ${CMAKE_SOURCE_DIR}/cube)
set(HAL ${CMAKE_SOURCE_DIR}/third_party/stm32cube_h7/Drivers)
set(RTOS ${CMAKE_SOURCE_DIR}/third_party/FreeRTOS-Kernel)
set(BOARD ${CMAKE_SOURCE_DIR}/components/board)
# FW pack 1.12.0's system_stm32h7xx.c has a wrong D1CorePrescTable (APB /2 read as /1, so every
# peripheral clock and baud rate is off by 2); both images use the fixed cmsis-device-h7 v1.10.7 one.
set(SYSTEM_C ${HAL}/CMSIS/Device/ST/STM32H7xx/Source/Templates/system_stm32h7xx.c)

set(CPU -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb)
add_compile_options(${CPU} -ffunction-sections -fdata-sections -g3)
add_compile_definitions(STM32H750xx USE_HAL_DRIVER USE_PWR_LDO_SUPPLY)
add_link_options(${CPU} --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage
                 -Wl,--no-warn-rwx-segments)
# Our own code is warning-free; vendored and generated code builds with the defaults.
set(STRICT -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror)

# HAL modules per image: the ones CubeMX copies for each .ioc.
set(HAL_COMMON cortex rcc rcc_ex flash flash_ex gpio hsem dma dma_ex mdma pwr pwr_ex exti i2c i2c_ex qspi uart uart_ex)
set(HAL_BOOT ${HAL_COMMON} iwdg)
set(HAL_APP ${HAL_COMMON} adc adc_ex eth eth_ex spi spi_ex tim tim_ex)

function(hal_sources out)
  set(list ${HAL}/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c)
  foreach(m ${ARGN})
    list(APPEND list ${HAL}/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_${m}.c)
  endforeach()
  set(${out} ${list} PARENT_SCOPE)
endfunction()

function(image_includes target cube_dir)
  target_include_directories(${target} PRIVATE
    ${cube_dir}/Inc
    ${HAL}/STM32H7xx_HAL_Driver/Inc
    ${HAL}/STM32H7xx_HAL_Driver/Inc/Legacy
    ${HAL}/CMSIS/Device/ST/STM32H7xx/Include
    ${HAL}/CMSIS/Include
    ${BOARD}/include)
endfunction()

function(bin_from_elf target)
  set_target_properties(${target} PROPERTIES SUFFIX .elf)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> ${CMAKE_BINARY_DIR}/${target}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>)
endfunction()

# ---- bootloader: everything CubeMX generates for boot.ioc, plus boot/ ----
file(GLOB BOOT_CUBE ${CUBE}/boot/Src/*.c)
list(REMOVE_ITEM BOOT_CUBE ${CUBE}/boot/Src/system_stm32h7xx.c)
list(APPEND BOOT_CUBE ${SYSTEM_C})
hal_sources(BOOT_HAL ${HAL_BOOT})
set(BOOT_OWN boot/boot.c boot/qspi_flash.c boot/jumpcheck.c ${BOARD}/board_io.c)
add_executable(boot ${BOOT_CUBE} ${CUBE}/boot/startup_stm32h750xx.s ${BOOT_HAL} ${BOOT_OWN})
image_includes(boot ${CUBE}/boot)
target_include_directories(boot PRIVATE boot)
set_source_files_properties(${BOOT_OWN} TARGET_DIRECTORY boot PROPERTIES COMPILE_OPTIONS "${STRICT}")
target_compile_options(boot PRIVATE -Os)
target_link_options(boot PRIVATE -T${CUBE}/boot/stm32h750vbtx_flash.ld -Wl,-Map=${CMAKE_BINARY_DIR}/boot.map)
set_target_properties(boot PROPERTIES LINK_DEPENDS ${CUBE}/boot/stm32h750vbtx_flash.ld)
bin_from_elf(boot)
```

The bootloader uses CubeMX's `syscalls.c` and `sysmem.c` from `cube/boot/Src`; it never allocates memory.

- [ ] **Step 3: Board I/O shared by both images**

Create `stm32/firmware/components/board/include/board.h`:

```c
// Board services shared by the bootloader and the app (spec §6.1, §11.2, §11.4).
#pragma once

// Kicks both watchdogs: toggles WDI (SGM706 through Q6, which also blinks LED WORK) and reloads
// IWDG1. Without R20 the SGM706 cannot reset the MCU, so IWDG1 is the one that does.
void board_kick(void);

// Latches D1 = D2 = 0 into the 74HC573 and enables its outputs (spec §11.2 step 2).
// MX_GPIO_Init must have run (it leaves OE high, D1/D2/LE low).
void board_latch_zero(void);
```

Create `stm32/firmware/components/board/board_io.c`:

```c
#include "board.h"
#include "main.h"  // pin labels from the .ioc; boot and app use the same names

void board_kick(void)
{
    HAL_GPIO_TogglePin(WDI_LED_GPIO_Port, WDI_LED_Pin);
    IWDG1->KR = 0x0000AAAAu;  // reload; harmless while IWDG1 is not running
}

void board_latch_zero(void)
{
    HAL_GPIO_WritePin(LATCH_OE_GPIO_Port, LATCH_OE_Pin, GPIO_PIN_SET);  // outputs hi-Z: relays released
    HAL_GPIO_WritePin(DO1_D_GPIO_Port, DO1_D_Pin | DO2_D_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_LE_GPIO_Port, LATCH_LE_Pin, GPIO_PIN_SET);  // transparent: Q follows D
    (void)LATCH_LE_GPIO_Port->ODR;                                       // the write has reached the port
    __DSB();
    HAL_GPIO_WritePin(LATCH_LE_GPIO_Port, LATCH_LE_Pin, GPIO_PIN_RESET);  // latch the zeros
    HAL_GPIO_WritePin(LATCH_OE_GPIO_Port, LATCH_OE_Pin, GPIO_PIN_RESET);  // and drive them
}
```

- [ ] **Step 4: GD25Q32E driver**

Create `stm32/firmware/boot/qspi_flash.h`:

```c
// GD25Q32E on QUADSPI bank 2 (spec §6.2): bring the flash to a known state, enable quad mode and map
// it at 0x90000000 with Quad I/O Fast Read (EBh). Values from GD25Q32E Rev 1.3 §6, §7.11, §7.29, §8.6.
#pragma once
#include <stdint.h>
#include "stm32h7xx_hal.h"

#define QF_JEDEC_GD25Q32E 0xC84016u

enum { QF_OK = 0, QF_EHAL = -1, QF_EID = -2, QF_EQE = -3, QF_ETIMEOUT = -4 };

typedef void (*qf_kick_fn)(void);

// Leaves continuous-read mode, resets the flash (66h/99h), checks the JEDEC id, sets QE if clear
// and reads the dummy configuration. kick is called while waiting.
int qf_init(QSPI_HandleTypeDef *h, qf_kick_fn kick);
uint32_t qf_read_jedec(QSPI_HandleTypeDef *h);  // 0 on a bus error
int qf_memory_mapped(QSPI_HandleTypeDef *h);
```

Create `stm32/firmware/boot/qspi_flash.c`:

```c
#include "qspi_flash.h"

#define PHASE_MS 100u  // HAL timeout of one command phase

static uint32_t s_dummy = 4;  // EBh dummy clocks after M7-0: 4 with DC = 0, 8 with DC = 1 (§6 table)

static QSPI_CommandTypeDef command(uint8_t ins)
{
    QSPI_CommandTypeDef c = {0};
    c.Instruction = ins;
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.AddressMode = QSPI_ADDRESS_NONE;
    c.AddressSize = QSPI_ADDRESS_24_BITS;
    c.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    c.DataMode = QSPI_DATA_NONE;
    c.DdrMode = QSPI_DDR_MODE_DISABLE;
    c.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    c.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    return c;
}

static int send(QSPI_HandleTypeDef *h, uint8_t ins)
{
    QSPI_CommandTypeDef c = command(ins);
    return HAL_QSPI_Command(h, &c, PHASE_MS) == HAL_OK ? QF_OK : QF_EHAL;
}

static int read_reg(QSPI_HandleTypeDef *h, uint8_t ins, uint8_t *out, uint32_t n)
{
    QSPI_CommandTypeDef c = command(ins);
    c.DataMode = QSPI_DATA_1_LINE;
    c.NbData = n;
    if (HAL_QSPI_Command(h, &c, PHASE_MS) != HAL_OK)
        return QF_EHAL;
    return HAL_QSPI_Receive(h, out, PHASE_MS) == HAL_OK ? QF_OK : QF_EHAL;
}

static int write_reg(QSPI_HandleTypeDef *h, uint8_t ins, uint8_t v)
{
    QSPI_CommandTypeDef c = command(ins);
    c.DataMode = QSPI_DATA_1_LINE;
    c.NbData = 1;
    if (HAL_QSPI_Command(h, &c, PHASE_MS) != HAL_OK)
        return QF_EHAL;
    return HAL_QSPI_Transmit(h, &v, PHASE_MS) == HAL_OK ? QF_OK : QF_EHAL;
}

// Polls WIP (SR1 bit 0) until clear.
static int wait_ready(QSPI_HandleTypeDef *h, uint32_t ms, qf_kick_fn kick)
{
    uint32_t t0 = HAL_GetTick();
    for (;;) {
        uint8_t sr1;
        if (read_reg(h, 0x05, &sr1, 1))
            return QF_EHAL;
        if (!(sr1 & 0x01))
            return QF_OK;
        if (kick)
            kick();
        if (HAL_GetTick() - t0 > ms)
            return QF_ETIMEOUT;
    }
}

uint32_t qf_read_jedec(QSPI_HandleTypeDef *h)
{
    uint8_t id[3];
    if (read_reg(h, 0x9F, id, 3))
        return 0;
    return (uint32_t)id[0] << 16 | (uint32_t)id[1] << 8 | id[2];
}

int qf_init(QSPI_HandleTypeDef *h, qf_kick_fn kick)
{
    // The MCU may have reset while a previous run left the flash in continuous-read mode, where it
    // takes the next clocks as an address. Ten clocks with IO0-IO3 high give it M7-0 = FFh, which
    // ends that mode (§7.11); a flash in normal mode sees an unknown command FFh and ignores it.
    // The ones go out as instruction (2 clocks) plus 32-bit alternate bytes (8 clocks): an address
    // phase cannot carry them, since QUADSPI rejects addresses beyond FSIZE (TEF).
    QSPI_CommandTypeDef c = command(0xFF);
    c.InstructionMode = QSPI_INSTRUCTION_4_LINES;
    c.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;
    c.AlternateBytesSize = QSPI_ALTERNATE_BYTES_32_BITS;
    c.AlternateBytes = 0xFFFFFFFFu;
    if (HAL_QSPI_Command(h, &c, PHASE_MS) != HAL_OK)
        return QF_EHAL;
    // Software reset (§7.29). tRST is 30 us, tRST_E 12 ms when an erase was interrupted.
    if (send(h, 0x66) || send(h, 0x99))
        return QF_EHAL;
    HAL_Delay(13);
    if (kick)
        kick();
    if (qf_read_jedec(h) != QF_JEDEC_GD25Q32E)
        return QF_EID;
    // QE is SR2 bit 1 (S9), non-volatile: written only when clear, keeping the other SR2 bits.
    uint8_t sr2;
    if (read_reg(h, 0x35, &sr2, 1))
        return QF_EHAL;
    if (!(sr2 & 0x02)) {
        if (send(h, 0x06) || write_reg(h, 0x31, (uint8_t)(sr2 | 0x02)))
            return QF_EHAL;
        int r = wait_ready(h, 60, kick);  // tW is 30 ms at most
        if (r)
            return r;
        if (read_reg(h, 0x35, &sr2, 1))
            return QF_EHAL;
        if (!(sr2 & 0x02))
            return QF_EQE;
    }
    // DC is SR3 bit 0 (S16). It stays as found: the default 0 allows 104 MHz, and QSPI runs at 80 MHz.
    uint8_t sr3;
    if (read_reg(h, 0x15, &sr3, 1))
        return QF_EHAL;
    s_dummy = (sr3 & 0x01) ? 8 : 4;
    return QF_OK;
}

int qf_memory_mapped(QSPI_HandleTypeDef *h)
{
    QSPI_CommandTypeDef c = command(0xEB);
    c.AddressMode = QSPI_ADDRESS_4_LINES;
    c.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;  // M7-0 on four lines (two clocks)
    c.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
    c.AlternateBytes = 0xFF;  // M5-4 != 10b: the flash never enters continuous-read mode
    c.DummyCycles = s_dummy;
    c.DataMode = QSPI_DATA_4_LINES;
    QSPI_MemoryMappedTypeDef m = {.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE, .TimeOutPeriod = 0};
    return HAL_QSPI_MemoryMapped(h, &c, &m) == HAL_OK ? QF_OK : QF_EHAL;
}
```

- [ ] **Step 5: The bootloader stub and its USER CODE hooks**

Create `stm32/firmware/boot/boot.h`:

```c
// Bootloader entry points, called from the USER CODE blocks of cube/boot/Src/main.c.
#pragma once

#define BOOT_VERSION "0.2.0"

void boot_early(void);  // USER CODE 1: before MPU_Config, HAL_Init and the clocks (spec §11.2 steps 1-2)
void boot_main(void);   // USER CODE 2: after the CubeMX init calls; never returns
```

Create `stm32/firmware/boot/boot.c`:

```c
// Bootloader stub of Plan 02 (spec §6.2, §11.2 boot sequence): watchdogs, safe outputs, clocks
// (CubeMX), QSPI in memory-mapped mode, then the jump to the XIP app. The OTA state machine of
// §11.2 replaces boot_main's middle part in Plan 04.
#include "boot.h"
#include <stdarg.h>
#include <stdio.h>
#include "board.h"
#include "gpio.h"
#include "jumpcheck.h"
#include "main.h"
#include "qspi_flash.h"
#include "quadspi.h"
#include "usart.h"

static void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_line(const char *fmt, ...)
{
    char line[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 1)
        n = (int)sizeof line - 1;
    if (n > 0)
        HAL_UART_Transmit(&huart7, (const uint8_t *)line, (uint16_t)n, 100);
}

// Halted rescue (spec §11.2): LED WORK blinks fast, both watchdogs stay fed, SWD can reflash.
static void __attribute__((noreturn)) rescue(const char *why, int code)
{
    log_line("boot: %s (%d), halted rescue\r\n", why, code);
    for (;;) {
        board_kick();
        HAL_Delay(100);
    }
}

// Hands over in the state of spec §6.2: interrupts off, SysTick off, nothing enabled or pending.
static void __attribute__((noreturn)) jump(uint32_t msp, uint32_t pc)
{
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    for (unsigned i = 0; i < sizeof NVIC->ICER / sizeof NVIC->ICER[0]; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    __DSB();
    __ISB();
    SCB->VTOR = APP_VECTORS;
    __asm volatile("msr msp, %0\n\tbx %1" : : "r"(msp), "r"(pc) : "memory");
    __builtin_unreachable();
}

void boot_early(void)
{
    MX_GPIO_Init();  // boot.ioc levels: latch OE high (hi-Z), D1/D2/LE low, modem keys low, PHY in reset
    board_kick();
}

void boot_main(void)
{
    board_kick();
    board_latch_zero();
    DBGMCU->APB4FZ1 |= DBGMCU_APB4FZ1_DBG_IWDG1;  // a core halted by the debugger does not trip IWDG1
    log_line("\r\nSBIOT-H750 boot " BOOT_VERSION " rsr=%08lx sysclk=%lu\r\n", (unsigned long)RCC->RSR,
             (unsigned long)HAL_RCC_GetSysClockFreq());
    int r = qf_init(&hqspi, board_kick);
    log_line("boot: qspi id %06lx\r\n", (unsigned long)qf_read_jedec(&hqspi));
    if (r)
        rescue("qspi init failed", r);
    r = qf_memory_mapped(&hqspi);
    if (r)
        rescue("qspi memory-mapped failed", r);
    const volatile uint32_t *v = (const volatile uint32_t *)APP_VECTORS;
    uint32_t msp = v[0], pc = v[1];
    if (!boot_app_ok(msp, pc)) {
        log_line("boot: no valid app (msp %08lx, reset %08lx)\r\n", (unsigned long)msp, (unsigned long)pc);
        rescue("no app", 0);
    }
    log_line("boot: jump msp %08lx pc %08lx\r\n", (unsigned long)msp, (unsigned long)pc);
    while (!(huart7.Instance->ISR & USART_ISR_TC)) {
    }
    board_kick();
    jump(msp, pc);
}
```

The bootloader leaves RCC_RSR as it is; the app reports it and clears it.

In `stm32/firmware/cube/boot/Src/main.c`, insert between the USER CODE markers, and nowhere else:
- `/* USER CODE BEGIN Includes */` … `END Includes`: `#include "boot.h"`
- `/* USER CODE BEGIN 1 */` … `END 1` (first lines of `main`): `  boot_early();`
- `/* USER CODE BEGIN 2 */` … `END 2` (after `MX_UART7_Init();`): `  boot_main();`

Then confirm that regeneration keeps them:

Run: `stm32/firmware/tools/cubegen.sh && grep -c "boot_early();\|boot_main();\|#include \"boot.h\"" stm32/firmware/cube/boot/Src/main.c`
Expected: `cubegen: boot ok`, `cubegen: app ok`, `3`.

- [ ] **Step 6: Build the bootloader**

Run: `cd stm32/firmware && cmake --preset h750 > /dev/null && cmake --build --preset boot 2>&1 | grep -E "warning|error|FLASH:"; cd -`
Expected: no `warning` or `error` lines, and `FLASH: ~18 KB of 128 KB` (≤ 64 KB, spec §5.1).

- [ ] **Step 7: Flashing script**

Create `stm32/firmware/tools/flash.sh` (mode 755):

```bash
#!/bin/bash
# Flashes over SWD with STM32CubeProgrammer (spec §12.4): boot.bin into internal flash, app.bin into
# QSPI bank 2 through the board's external loader, then resets the board. Run from WSL.
#   stm32/firmware/tools/flash.sh boot|app|both|blank [BUILD_DIR]
# "blank" programs the first 4 KB of the QSPI slot to 0xFF (header and vector table erased).
set -euo pipefail
what=${1:?boot, app, both or blank}
build=$(cd "${2:-$(dirname "$0")/../build/h750}" && pwd)
CLI=${CUBEPROG_CLI:-"/mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"}
LOADER="$(dirname "$CLI")/ExternalLoader/STM32H750VBT6_GD25Q32ESIG.stldr"
# The team's loader for this board (Dev_info: 0x90000000, 4 MB, 256 B pages, 1024 x 4 KB sectors).
# Proved on the spare block 0x903E0000 before first use (Plan 02, Task 4). Plan 04 replaces it.
LOADER_SHA256=5ef2de362969bb2ad9d0c92604e58f27b018614533fe421e6ac5974c9bcb0699
echo "$LOADER_SHA256  $LOADER" | sha256sum -c --quiet - || { echo "flash: the loader is not the pinned build"; exit 1; }
# CubeProgrammer is a Windows program: it reads the images from the Windows profile and runs from C:.
stage=$(wslpath -u "$(cd /mnt/c && powershell.exe -NoProfile -Command '$env:USERPROFILE' | tr -d '\r')")/sbiot
mkdir -p "$stage"
cd /mnt/c
prog() {
    local out
    out=$("$CLI" "$@" 2>&1) || true
    if ! grep -q 'Download verified successfully' <<<"$out"; then
        grep -iE 'error|fail' <<<"$out" || tail -5 <<<"$out"
        echo "flash: FAILED"
        exit 1
    fi
}
qspi() {  # $1 = local file, written at 0x90000000
    cp "$1" "$stage/$(basename "$1")"
    prog -c port=SWD mode=UR -el "$(wslpath -w "$LOADER")" -w "$(wslpath -w "$stage/$(basename "$1")")" 0x90000000 -v
    echo "flash: $(basename "$1") ($(stat -c %s "$1") bytes) at 0x90000000 verified"
}
case "$what" in boot|both)
    cp "$build/boot.bin" "$stage/boot.bin"
    prog -c port=SWD mode=UR -w "$(wslpath -w "$stage/boot.bin")" 0x08000000 -v
    echo "flash: boot.bin ($(stat -c %s "$build/boot.bin") bytes) at 0x08000000 verified" ;;
esac
case "$what" in app|both) qspi "$build/app.bin" ;; esac
if [ "$what" = blank ]; then
    head -c 4096 /dev/zero | tr '\0' '\377' > "$build/blank4k.bin"
    qspi "$build/blank4k.bin"
fi
"$CLI" -c port=SWD mode=HOTPLUG -hardRst > /dev/null 2>&1
echo "flash: board reset"
```

- [ ] **Step 8: Bench access to UART7**

Create `stm32/firmware/test/bench/board.py`:

```python
"""Bench access from WSL to the board's UART7 (ST-Link VCP, COM4 on Windows) and to its reset.

The COM port belongs to Windows, so a PowerShell process reads it; every received line comes back
with the milliseconds since the port was opened. Set SBIOT_COM to use another port.
"""
import base64
import os
import subprocess
import threading
import time

COM = os.environ.get('SBIOT_COM', 'COM4')
CLI = os.environ.get('CUBEPROG_CLI',
                     '/mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe')

# Two flat arrays for the timed sends: PowerShell unrolls a one-element array of pairs.
_PS = r'''
$p = New-Object System.IO.Ports.SerialPort '%(port)s', 115200
$p.Open(); $w = [Diagnostics.Stopwatch]::StartNew(); $buf = ''
$at = @(%(at)s); $tx = @(%(tx)s); $i = 0
while ($w.ElapsedMilliseconds -lt %(ms)d) {
  if ($i -lt $at.Count -and $w.ElapsedMilliseconds -ge $at[$i]) { $p.Write($tx[$i]); $i++ }
  $buf += $p.ReadExisting()
  while (($n = $buf.IndexOf("`n")) -ge 0) {
    $line = $buf.Substring(0, $n).TrimEnd("`r"); $buf = $buf.Substring($n + 1)
    [Console]::Out.WriteLine([string]$w.ElapsedMilliseconds + '|' + $line)
  }
  Start-Sleep -Milliseconds 5
}
$p.Close()
'''


def listen(seconds, sends=(), during=None):
    """Reads UART7 for `seconds`. sends[i] = (at_s, text) is written at at_s; during() runs once the
    port is open. Returns [(ms, line)]."""
    at = ','.join(str(int(t * 1000)) for t, _ in sends)
    tx = ','.join("'%s'" % text for _, text in sends)
    script = _PS % {'port': COM, 'ms': int(seconds * 1000), 'at': at, 'tx': tx}
    enc = base64.b64encode(script.encode('utf-16-le')).decode()
    proc = subprocess.Popen(['powershell.exe', '-NoProfile', '-EncodedCommand', enc], cwd='/mnt/c',
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    if during:
        time.sleep(1.5)  # PowerShell needs about a second to open the port
        threading.Thread(target=during).start()
    lines = []
    for raw in proc.stdout:
        ms, sep, line = raw.rstrip('\n').partition('|')
        if sep and ms.isdigit():
            lines.append((int(ms), line))
    proc.wait()
    return lines


def hard_reset():
    """Pulses NRST through the ST-Link."""
    subprocess.run([CLI, '-c', 'port=SWD', 'mode=HOTPLUG', '-hardRst'], cwd='/mnt/c', capture_output=True, check=True)


if __name__ == '__main__':  # python3 board.py SECONDS: print what UART7 says after a reset
    import sys
    for ms, line in listen(float(sys.argv[1]) if len(sys.argv) > 1 else 5, during=hard_reset):
        print('%6d %s' % (ms, line))
```

- [ ] **Step 9: Prove the loader before trusting it (B5, B7)**

Program 64 KB of a known pattern into the spare block 0x903E0000, read it back and compare. The block is unused by the slot (the slot ends at 0x90170000).

```bash
S=/mnt/c/Users/linh_bui/sbiot; mkdir -p $S
python3 -c "open('$S/pattern.bin','wb').write(bytes(((i*7+i//256*13)&0xFF) for i in range(65536)))"
CLI="/mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"
L='C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\ExternalLoader\STM32H750VBT6_GD25Q32ESIG.stldr'
(cd /mnt/c && "$CLI" -c port=SWD mode=UR -el "$L" -w 'C:\Users\linh_bui\sbiot\pattern.bin' 0x903E0000 -v 2>&1 | grep -E "verified|Error")
(cd /mnt/c && "$CLI" -c port=SWD mode=UR -el "$L" -u 0x903E0000 0x10000 'C:\Users\linh_bui\sbiot\readback.bin' 2>&1 | grep -E "Error" ; true)
cmp $S/pattern.bin $S/readback.bin && echo LOADER_PROVED
```

Expected: `Download verified successfully`, then `LOADER_PROVED`.

- [ ] **Step 10: First boot, with the QSPI slot blanked**

The bootloader must identify the flash, map it and refuse the erased vector table (Review Focus 2). It must stay in halted rescue, feeding the watchdogs: no reset follows.

Run: `stm32/firmware/tools/flash.sh blank && stm32/firmware/tools/flash.sh boot && python3 stm32/firmware/test/bench/board.py 6`
Expected:
- The flashing lines: `flash: blank4k.bin (4096 bytes) at 0x90000000 verified`, `flash: boot.bin (… bytes) at 0x08000000 verified`, `flash: board reset`.
- Exactly one boot in 6 s. Its log lines are:
  ```
    SBIOT-H750 boot 0.2.0 rsr=00460000 sysclk=480000000
    boot: qspi id c84016
    boot: no valid app (msp ffffffff, reset ffffffff)
    boot: no app (0), halted rescue
  ```
  `rsr` has PINRSTF (bit 22) set. A second `SBIOT-H750 boot` line would mean a watchdog reset, which is a failure.

- [ ] **Step 11: Commit**

```bash
git add stm32/firmware/cmake stm32/firmware/CMakePresets.json stm32/firmware/CMakeLists.txt \
        stm32/firmware/components/board/include/board.h stm32/firmware/components/board/board_io.c \
        stm32/firmware/boot stm32/firmware/cube/boot/Src/main.c stm32/firmware/tools/flash.sh stm32/firmware/test/bench/board.py
git commit -m "stm32 boot: bootloader stub (PLL1, IWDG1, GD25Q32E in memory-mapped EBh at 80 MHz, jump check), CubeProgrammer flashing, bench UART"
```

### Task 5: The XIP app (startup, MPU, ITCM, FreeRTOS, wdog, now_us, log) and the ELF gate

**Files:**
- Create: `stm32/firmware/tools/check_elf.py`
- Create: `stm32/firmware/app/startup_app.h`, `app/startup_app.c`, `app/mpu_app.h`, `app/mpu_app.c`, `app/app.ld`, `app/image_header.c`, `app/hal_tick.c`, `app/FreeRTOSConfig.h`, `app/app_main.h`, `app/app_main.c`
- Create: `stm32/firmware/components/board/include/{now_us,wdog,blog}.h`, `components/board/{now_us,wdog,blog,syscalls}.c`
- Modify: `stm32/firmware/CMakeLists.txt` (append the app target)
- Modify: `stm32/firmware/cube/app/Src/main.c` (USER CODE `Includes`, `2`), `stm32/firmware/cube/app/Src/tim.c` (USER CODE `TIM2_Init 2`)

**Interfaces:**
- Consumes:
  - from Task 2: `cube/app` with `MX_GPIO_Init`, `MX_TIM2_Init`, `MX_UART7_Init`, `htim2`, `huart7`, the startup file's `g_pfnVectors`, `SystemCoreClockUpdate`;
  - from Task 3: `tick64_update`, `crc32_update`;
  - from Task 4: `board_kick`, `board_latch_zero`, `flash.sh`, `board.listen`.
- Produces:
  - **Time:** `uint64_t now_us(void);`, plus `HAL_GetTick()` = `now_us()/1000`.
  - **Watchdog task:** `void wdog_task(void *);`, `void wdog_stop(void);`, `uint32_t wdog_max_gap_us(void);`.
  - **Log:** `void blog_init(void);`, `void blog(const char *fmt, ...);`.
  - **Startup results:** `g_handoff_faults` (bits `HANDOFF_*`), `g_mpu_boot_match`.
  - **Linker symbols:** `__image_start__`, `__image_end__`.
  - **Sections:** `.itcm_text`, `.axi_bss`, `.sram1_bss`, `.sram23_bss`, `.sram4_bss`, `.adc_buf`, `.bkp_noinit`.
  - **UART7 console:** `i`, `r`, `w`, `c`.
  - **App log lines:**
    - `SBIOT-H750 app 0.2.0 rsr=XXXXXXXX sysclk=480000000`
    - `app: handoff ok (00), mpu regions 0-1 match boot`
    - `app: itcm vTaskSwitchContext=0x… memcpy=0x…`
    - `app: uptime_ms=N ticks=N wdog_max_gap_us=N`
    - `app: image crc32=XXXXXXXX len=N in N us`
    - `app: software reset`
    - `app: watchdog kicks stopped at N ms`

- [ ] **Step 1: Write the ELF gate first**

Create `stm32/firmware/tools/check_elf.py`:

```python
#!/usr/bin/env python3
"""Layout and ownership gates for boot.elf and app.elf (spec §5.1, §5.3, §6.2).

    python3 stm32/firmware/tools/check_elf.py BUILD_DIR

Prints one line per check and "elf: ok" when all pass; exits 1 otherwise. Needs arm-none-eabi-nm,
-readelf and -objdump on PATH (source ~/opt/xpack/env.sh).
"""
import re
import subprocess
import sys

# The app must not configure what the bootloader owns (spec §6.2):
# - no clock or QSPI setup;
# - no CubeMX MPU_Config (Reset_Handler programs the MPU once);
# - no CubeMX/CMSIS SystemInit or ExitRun0Mode, which left with the weak Reset_Handler that called them.
APP_FORBIDDEN = [
    'SystemInit', 'ExitRun0Mode', 'SystemClock_Config', 'MPU_Config', 'HAL_RCC_OscConfig',
    'HAL_RCC_ClockConfig', 'HAL_RCC_DeInit', 'HAL_PWREx_ConfigSupply', 'HAL_QSPI_Init',
    'HAL_QSPI_MemoryMapped', 'MX_QUADSPI_Init', 'HAL_MPU_ConfigRegion',
]
# Hot code that must run from ITCM (spec §5.3), above the 1 KB null guard.
APP_ITCM = ['vTaskSwitchContext', 'xTaskIncrementTick', 'PendSV_Handler', 'SysTick_Handler', 'SVC_Handler',
            'memcpy', 'memset']
ITCM = (0x00000400, 0x00010000)
DTCM = (0x20000000, 0x20020000)
DTCM_BUDGET = 120 * 1024
QSPI_SLOT = (0x90000000, 0x90000000 + 1472 * 1024)
BOOT_MAX = 64 * 1024


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def symbols(elf):
    out = {}
    for line in run('arm-none-eabi-nm', elf).splitlines():
        parts = line.split()
        if len(parts) == 3:
            out[parts[2]] = int(parts[0], 16)
    return out


def sections(elf):
    """name -> (addr, size) for allocated sections."""
    out = {}
    for line in run('arm-none-eabi-readelf', '-SW', elf).splitlines():
        m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]{8})\s+[0-9a-f]+\s+([0-9a-f]+)\s+\S+\s+(\S*A\S*)', line)
        if m:
            out[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return out


def calls_in(elf, func):
    """Names called (bl/blx/b.w) from the body of func, or None when func is missing."""
    dis = run('arm-none-eabi-objdump', '-d', '--no-show-raw-insn', elf)
    body = re.search(r'<%s>:\n(.*?)\n\n' % re.escape(func), dis, re.S)
    return set(re.findall(r'\s(?:bl|blx|b\.w)\s+[0-9a-f]+ <([^>+]+)', body.group(1))) if body else None


def main():
    build = sys.argv[1]
    failures = []

    def check(ok, what):
        print(('ok   ' if ok else 'FAIL ') + what)
        if not ok:
            failures.append(what)

    bs = sections(build + '/boot.elf')
    end = max(a + s for a, s in bs.values() if 0x08000000 <= a < 0x08020000)
    check(bs['.isr_vector'][0] == 0x08000000, 'boot: vector table at 0x08000000')
    check(end - 0x08000000 <= BOOT_MAX, 'boot: %d bytes of internal flash <= %d' % (end - 0x08000000, BOOT_MAX))

    app = build + '/app.elf'
    sym = symbols(app)
    sec = sections(app)
    for name in APP_FORBIDDEN:
        check(name not in sym, 'app: no %s' % name)
    for name in APP_ITCM:
        a = sym.get(name, 0)
        check(ITCM[0] <= a < ITCM[1], 'app: %s in ITCM (0x%08x)' % (name, a))
    check(sec.get('.image_header') == (0x90000000, 0x400), 'app: 1 KB header at 0x90000000')
    check(sec.get('.isr_vector', (0, 0))[0] == 0x90000400, 'app: vector table at 0x90000400')
    called = calls_in(app, 'Reset_Handler')
    # A call may go through a long-branch veneer (__memset_veneer), so match on the name.
    mem = [c for c in called or [] if re.search(r'mem(cpy|set|move)', c)]
    check(called is not None and not mem,
          'app: Reset_Handler calls no memcpy/memset (calls: %s)' % ', '.join(sorted(called or [])))
    check(not any(c.startswith('HAL_') for c in called or []), 'app: Reset_Handler calls no HAL function')
    dtcm = sum(s for a, s in sec.values() if DTCM[0] <= a < DTCM[1]) + 4096
    check(dtcm <= DTCM_BUDGET, 'app: DTCM %d bytes (data + bss + 4 KB MSP) <= %d' % (dtcm, DTCM_BUDGET))
    image_end = sym.get('__image_end__', 0)
    check(QSPI_SLOT[0] < image_end <= QSPI_SLOT[1], 'app: image ends at 0x%08x, inside the slot' % image_end)

    print('elf: ok' if not failures else 'elf: %d check(s) failed' % len(failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
```

- [ ] **Step 2: Run it to see it fail**

Run: `python3 stm32/firmware/tools/check_elf.py stm32/firmware/build/h750`
Expected: a `CalledProcessError` from `arm-none-eabi-nm` on the missing `app.elf`, after the two `ok boot:` lines.

- [ ] **Step 3: Time, watchdog task, log and newlib hooks**

Create `stm32/firmware/components/board/include/now_us.h`:

```c
// Microseconds since TIM2 started (spec §4). Safe from tasks and ISRs. The wdog task calls it every
// 200 ms, far inside the 71-minute limit of tick64_update().
#pragma once
#include <stdint.h>

uint64_t now_us(void);
```

Create `stm32/firmware/components/board/now_us.c`:

```c
#include "now_us.h"
#include "stm32h7xx.h"
#include "tick64.h"

static tick64_t s_tick;

uint64_t now_us(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint64_t t = tick64_update(&s_tick, TIM2->CNT);
    __set_PRIMASK(primask);
    return t;
}
```

Create `stm32/firmware/components/board/include/wdog.h`:

```c
// wdog task (spec §6.1, §11.4): kicks both watchdogs every 200 ms at priority 31.
#pragma once
#include <stdint.h>

void wdog_task(void *arg);
// Stops kicking for good; IWDG1 resets the board about 1.6 s later (bench test, TRIAL deadlines).
void wdog_stop(void);
// Largest interval between two kicks since boot, in microseconds (bench criterion wdiMaxGapMs, §6.1).
uint32_t wdog_max_gap_us(void);
```

Create `stm32/firmware/components/board/wdog.c`:

```c
#include "wdog.h"
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "now_us.h"

static volatile bool s_stop;
static volatile uint32_t s_max_gap_us;

void wdog_stop(void)
{
    s_stop = true;
}

uint32_t wdog_max_gap_us(void)
{
    return s_max_gap_us;
}

void wdog_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    uint64_t last = now_us();
    for (;;) {
        uint64_t t = now_us();  // also keeps the 64-bit extension of TIM2 current
        if (!s_stop) {
            board_kick();
            if (t - last > s_max_gap_us)
                s_max_gap_us = (uint32_t)(t - last);
            last = t;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(200));
    }
}
```

Create `stm32/firmware/components/board/include/blog.h`:

```c
// Plain UART7 log for bring-up (spec §12.4: 115200 baud). Plan 05 puts syslog in front of it.
// newlib-nano's printf has no %llu: print 64-bit values as 32-bit milliseconds.
#pragma once

void blog_init(void);  // before the scheduler starts; creates the mutex
void blog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
```

Create `stm32/firmware/components/board/blog.c`:

```c
#include "blog.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "usart.h"  // huart7 (CubeMX)

static StaticSemaphore_t s_mutex_mem;
static SemaphoreHandle_t s_mutex;

void blog_init(void)
{
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_mem);
}

void blog(const char *fmt, ...)
{
    char line[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof line)
        n = sizeof line - 1;
    bool locked = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (locked)
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    HAL_UART_Transmit(&huart7, (const uint8_t *)line, (uint16_t)n, 100);
    if (locked)
        xSemaphoreGive(s_mutex);
}
```

Create `stm32/firmware/components/board/syscalls.c`:

```c
// newlib I/O hooks of the app (the bootloader uses CubeMX's syscalls.c and sysmem.c). There is no
// file I/O, so these fail cleanly instead of linking nosys.specs' warning stubs. _sbrk always fails
// (spec §5.5): no allocation can escape the heap that Plan 05 routes malloc to.
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

int _close(int fd)
{
    (void)fd;
    errno = ENOSYS;
    return -1;
}

off_t _lseek(int fd, off_t off, int whence)
{
    (void)fd;
    (void)off;
    (void)whence;
    errno = ENOSYS;
    return -1;
}

int _read(int fd, void *buf, size_t len)
{
    (void)fd;
    (void)buf;
    (void)len;
    errno = ENOSYS;
    return -1;
}

int _write(int fd, const void *buf, size_t len)
{
    (void)fd;
    (void)buf;
    (void)len;
    errno = ENOSYS;
    return -1;
}

void *_sbrk(ptrdiff_t incr)
{
    (void)incr;
    errno = ENOMEM;
    return (void *)-1;
}
```

- [ ] **Step 4: Startup, MPU, image header and HAL timebase**

Create `stm32/firmware/app/startup_app.h`:

```c
// What Reset_Handler saw and did before main() (spec §6.2), for the boot log.
#pragma once
#include <stdint.h>

// Bits of the handoff state that were NOT as the bootloader promises (0 = all as specified).
enum {
    HANDOFF_SYSCLK_NOT_PLL1 = 1u << 0,
    HANDOFF_QSPI_NOT_MAPPED = 1u << 1,
    HANDOFF_IRQ_ENABLED = 1u << 2,
    HANDOFF_SYSTICK_ON = 1u << 3,
    HANDOFF_NVIC_ACTIVE = 1u << 4,
};
extern uint32_t g_handoff_faults;
// 1 when MPU regions 0 and 1 left by the bootloader equal the app's own (spec §6.2).
extern uint32_t g_mpu_boot_match;
```

Create `stm32/firmware/app/mpu_app.h`:

```c
// The app's only MPU programming (spec §6.2 step 4), done by Reset_Handler before any DMA exists.
#pragma once
#include <stdint.h>

// Returns 1 when the bootloader's regions 0 and 1 match the app's, then programs the whole table.
uint32_t mpu_app_setup(void);
```

Create `stm32/firmware/app/mpu_app.c`:

```c
#include "mpu_app.h"
#include "stm32h7xx.h"

// Regions 0 and 1 repeat the bootloader's MPU_Config (cube/boot, from boot.ioc):
//   0: CubeMX background region, 4 GB with sub-regions 0, 1, 2 and 7 disabled, so
//      0x60000000-0xDFFFFFFF is no-access and never fetched speculatively;
//   1: the 4 MB QSPI window, read-only, cacheable, executable.
// A higher region number wins where regions overlap.
static const ARM_MPU_Region_t k_regions[] = {
    {ARM_MPU_RBAR(0, 0x00000000u), ARM_MPU_RASR(1, ARM_MPU_AP_NONE, 0, 1, 0, 0, 0x87, ARM_MPU_REGION_SIZE_4GB)},
    {ARM_MPU_RBAR(1, 0x90000000u), ARM_MPU_RASR(0, ARM_MPU_AP_RO, 0, 0, 1, 0, 0x00, ARM_MPU_REGION_SIZE_4MB)},
    // SRAM1: DMA buffers (spec §5.3, §5.4), Normal non-cacheable (TEX 1, C 0, B 0), never executed
    {ARM_MPU_RBAR(2, 0x30000000u), ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 0, 0, 0, 0x00, ARM_MPU_REGION_SIZE_128KB)},
    // the ADC3 BDMA buffer (spec §6.6), Normal non-cacheable, never executed
    {ARM_MPU_RBAR(3, 0x3800FC00u), ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 0, 0, 0, 0x00, ARM_MPU_REGION_SIZE_1KB)},
    // null-pointer guard: the first 1 KB of ITCM; .itcm_text starts at 0x400
    {ARM_MPU_RBAR(4, 0x00000000u), ARM_MPU_RASR(1, ARM_MPU_AP_NONE, 0, 0, 0, 0, 0x00, ARM_MPU_REGION_SIZE_1KB)},
};

uint32_t mpu_app_setup(void)
{
    uint32_t match = 1;
    for (uint32_t i = 0; i < 2; i++) {
        MPU->RNR = i;
        if ((MPU->RBAR & MPU_RBAR_ADDR_Msk) != (k_regions[i].RBAR & MPU_RBAR_ADDR_Msk) || MPU->RASR != k_regions[i].RASR)
            match = 0;
    }
    ARM_MPU_Disable();
    for (uint32_t i = 0; i < 16; i++)
        ARM_MPU_ClrRegion(i);
    ARM_MPU_Load(k_regions, sizeof k_regions / sizeof k_regions[0]);
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
    return match;
}
```

Create `stm32/firmware/app/startup_app.c`:

```c
// XIP app entry (spec §6.2). The bootloader already runs PLL1 and maps the QSPI, so nothing here
// touches RCC clock setup, QUADSPI or pinmux. CubeMX's startup file still supplies the vector
// table; its weak Reset_Handler is replaced by this one, and its SystemInit is never called.
// This file is built with -fno-tree-loop-distribute-patterns: the copy loops must not become
// memcpy/memset calls, because those run from ITCM, which these loops fill.
#include <stdint.h>
#include "mpu_app.h"
#include "startup_app.h"
#include "stm32h7xx.h"

extern const uint32_t __app_copy_start__[], __app_copy_end__[];  // {load, run, bytes} triples
extern const uint32_t __app_zero_start__[], __app_zero_end__[];  // {run, bytes} pairs
extern const uint32_t g_pfnVectors[];
extern void __libc_init_array(void);
extern int main(void);

uint32_t g_handoff_faults;
uint32_t g_mpu_boot_match;

static uint32_t handoff_faults(void)
{
    uint32_t f = 0;
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL1)
        f |= HANDOFF_SYSCLK_NOT_PLL1;
    if ((QUADSPI->CCR & QUADSPI_CCR_FMODE) != QUADSPI_CCR_FMODE)
        f |= HANDOFF_QSPI_NOT_MAPPED;
    if (!__get_PRIMASK())
        f |= HANDOFF_IRQ_ENABLED;
    if (SysTick->CTRL & SysTick_CTRL_ENABLE_Msk)
        f |= HANDOFF_SYSTICK_ON;
    for (unsigned i = 0; i < sizeof NVIC->ISER / sizeof NVIC->ISER[0]; i++)
        if (NVIC->ISER[i] || NVIC->ISPR[i])
            f |= HANDOFF_NVIC_ACTIVE;
    return f;
}

void Reset_Handler(void)
{
    SCB->CPACR |= 0xFu << 20;  // CP10/CP11 full access: the FPU, before any code that may use it
    __DSB();
    __ISB();
    uint32_t faults = handoff_faults();  // before anything below changes that state
    RCC->AHB2ENR |= RCC_AHB2ENR_D2SRAM1EN | RCC_AHB2ENR_D2SRAM2EN | RCC_AHB2ENR_D2SRAM3EN;
    RCC->AHB4ENR |= RCC_AHB4ENR_BKPRAMEN;
    (void)RCC->AHB4ENR;  // the clocks are on before the first access
    SCB->VTOR = (uint32_t)g_pfnVectors;
    for (const uint32_t *t = __app_copy_start__; t < __app_copy_end__; t += 3) {
        const uint32_t *src = (const uint32_t *)t[0];
        uint32_t *dst = (uint32_t *)t[1];
        for (uint32_t n = t[2] / 4; n; n--)
            *dst++ = *src++;
    }
    for (const uint32_t *t = __app_zero_start__; t < __app_zero_end__; t += 2) {
        uint32_t *dst = (uint32_t *)t[0];
        for (uint32_t n = t[1] / 4; n; n--)
            *dst++ = 0;
    }
    g_handoff_faults = faults;
    g_mpu_boot_match = mpu_app_setup();
    SCB_EnableICache();
    SCB_EnableDCache();
    SystemCoreClockUpdate();  // reads RCC only
    __libc_init_array();
    main();
    for (;;) {
    }
}
```

The tables are named `__app_copy_*` and `__app_zero_*` because CMSIS 5.9 already declares `__copy_table_start__` and the other `__*_table_*` names with another type.

Create `stm32/firmware/app/image_header.c`:

```c
// The 1 KB image header at 0x90000000 (spec §11.1). mkimage.py fills it in Plan 04; until then it
// reads as erased flash.
#include <stdint.h>

__attribute__((section(".image_header"), used)) const uint8_t image_header[1024] = {[0 ... 1023] = 0xFF};
```

Create `stm32/firmware/app/hal_tick.c`:

```c
// The app's HAL timebase is TIM2 through now_us(): SysTick belongs to FreeRTOS, and no interrupt
// is needed, so HAL timeouts also work before the scheduler starts and inside critical sections.
#include "now_us.h"
#include "stm32h7xx_hal.h"

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    (void)TickPriority;
    return HAL_OK;
}

uint32_t HAL_GetTick(void)
{
    return (uint32_t)(now_us() / 1000u);
}
```

- [ ] **Step 5: Linker script**

Create `stm32/firmware/app/app.ld`:

```ld
/* XIP app (spec §5.1, §5.3, §6.2): 1 KB header and vector table at 0x90000000/0x90000400 in QSPI,
   hot code copied to ITCM, data and task stacks in DTCM, buffers in the other RAMs by section. */
ENTRY(Reset_Handler)

MEMORY
{
  QSPI    (rx)  : ORIGIN = 0x90000000, LENGTH = 1472K  /* the slot: header + body */
  ITCM    (rwx) : ORIGIN = 0x00000400, LENGTH = 63K    /* 0x0-0x3FF is the MPU null guard */
  DTCM    (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
  AXI     (rw)  : ORIGIN = 0x24000000, LENGTH = 512K
  SRAM1   (rw)  : ORIGIN = 0x30000000, LENGTH = 128K   /* non-cacheable (MPU region 2) */
  SRAM23  (rw)  : ORIGIN = 0x30020000, LENGTH = 160K
  SRAM4   (rw)  : ORIGIN = 0x38000000, LENGTH = 63K
  ADCBUF  (rw)  : ORIGIN = 0x3800FC00, LENGTH = 1K     /* non-cacheable (MPU region 3) */
  BKPSRAM (rw)  : ORIGIN = 0x38800000, LENGTH = 4K
}

_estack = ORIGIN(DTCM) + LENGTH(DTCM);  /* MSP: 4 KB at the top of DTCM (spec §5.3) */
_Min_Stack_Size = 4K;
__image_start__ = ORIGIN(QSPI);

SECTIONS
{
  .image_header :
  {
    KEEP(*(.image_header))
    . = 0x400;
  } >QSPI

  .isr_vector :
  {
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >QSPI

  /* Before .text, which would otherwise take these input sections first. */
  .itcm_text :
  {
    . = ALIGN(4);
    *tasks.c.obj(.text .text.*)
    *queue.c.obj(.text .text.*)
    *list.c.obj(.text .text.*)
    *timers.c.obj(.text .text.*)
    *port.c.obj(.text .text.*)
    *_nano.a:libc_a-memcpy*.o(.text .text.*)  /* libg_nano.a with --specs=nano.specs */
    *_nano.a:libc_a-memset.o(.text .text.*)
    *(.itcm_text .itcm_text.*)  /* ISRs tagged by later plans */
    . = ALIGN(4);
  } >ITCM AT> QSPI

  .text :
  {
    . = ALIGN(4);
    *(.text .text.*)
    *(.glue_7) *(.glue_7t) *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
  } >QSPI

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } >QSPI

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >QSPI
  .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >QSPI

  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } >QSPI
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >QSPI
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } >QSPI

  /* Reset_Handler walks these tables (startup_app.c). */
  .copy.table :
  {
    . = ALIGN(4);
    __app_copy_start__ = .;
    LONG(LOADADDR(.itcm_text)) LONG(ADDR(.itcm_text)) LONG(SIZEOF(.itcm_text))
    LONG(LOADADDR(.data))      LONG(ADDR(.data))      LONG(SIZEOF(.data))
    __app_copy_end__ = .;
  } >QSPI
  .zero.table :
  {
    . = ALIGN(4);
    __app_zero_start__ = .;
    LONG(ADDR(.bss))        LONG(SIZEOF(.bss))
    LONG(ADDR(.axi_bss))    LONG(SIZEOF(.axi_bss))
    LONG(ADDR(.sram1_bss))  LONG(SIZEOF(.sram1_bss))
    LONG(ADDR(.sram23_bss)) LONG(SIZEOF(.sram23_bss))
    LONG(ADDR(.sram4_bss))  LONG(SIZEOF(.sram4_bss))
    LONG(ADDR(.adc_buf))    LONG(SIZEOF(.adc_buf))
    __app_zero_end__ = .;
  } >QSPI

  /* CubeMX's weak Reset_Handler names these; it is replaced, but keep them defined. */
  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data .data.*)
    . = ALIGN(4);
    _edata = .;
  } >DTCM AT> QSPI
  __image_end__ = LOADADDR(.data) + SIZEOF(.data);

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >DTCM

  .axi_bss (NOLOAD)    : { . = ALIGN(32); *(.axi_bss .axi_bss.*)       . = ALIGN(32); } >AXI
  .sram1_bss (NOLOAD)  : { . = ALIGN(32); *(.sram1_bss .sram1_bss.*)   . = ALIGN(32); } >SRAM1
  .sram23_bss (NOLOAD) : { . = ALIGN(32); *(.sram23_bss .sram23_bss.*) . = ALIGN(32); } >SRAM23
  .sram4_bss (NOLOAD)  : { . = ALIGN(32); *(.sram4_bss .sram4_bss.*)   . = ALIGN(32); } >SRAM4
  .adc_buf (NOLOAD)    : { . = ALIGN(32); *(.adc_buf .adc_buf.*)       . = ALIGN(32); } >ADCBUF
  .bkp_noinit (NOLOAD) : { *(.bkp_noinit .bkp_noinit.*) } >BKPSRAM   /* survives resets; never zeroed */
}

/* DTCM budget (spec §5.3): .data, .bss (which holds the static task stacks) and the MSP <= 120 KB. */
ASSERT(SIZEOF(.data) + SIZEOF(.bss) + _Min_Stack_Size <= 120K, "DTCM budget of 120 KB exceeded")
ASSERT(_ebss <= _estack - _Min_Stack_Size, "no room for the 4 KB MSP stack")
ASSERT(ADDR(.isr_vector) == 0x90000400, "vector table must start at 0x90000400")
```

- [ ] **Step 6: FreeRTOS configuration and the app tasks**

Create `stm32/firmware/app/FreeRTOSConfig.h`:

```c
// FreeRTOS V11.3.1 for the app (spec §6.1). Plan 02 has only static objects; Plan 05 adds heap_5.
#pragma once

#include <stdint.h>
extern uint32_t SystemCoreClock;

#define configCPU_CLOCK_HZ (SystemCoreClock)
#define configTICK_RATE_HZ 1000
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configMAX_PRIORITIES 32
#define configMINIMAL_STACK_SIZE 256  // words
#define configMAX_TASK_NAME_LEN 12
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_NEWLIB_REENTRANT 0  // Plan 05 turns it on with the heap_5 wrapper (spec §5.5)
#define configENABLE_BACKWARD_COMPATIBILITY 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_MALLOC_FAILED_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configRECORD_STACK_HIGH_ADDRESS 1
#define configUSE_TRACE_FACILITY 0
#define configGENERATE_RUN_TIME_STATS 0

#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configKERNEL_PROVIDED_STATIC_MEMORY 1  // idle and timer task memory come from the kernel

// Timer task: 2 KB, priority 30; its callbacks only send notifications (spec §6.1).
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 30
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH 512

// Cortex-M7 with 4 priority bits. ISRs at priority 5..15 may call FromISR APIs; 0..4 never do.
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

// A failed assertion stops everything; wdog stops with it and IWDG1 resets the board.
#define configASSERT(x)               \
    do {                              \
        if (!(x)) {                   \
            taskDISABLE_INTERRUPTS(); \
            for (;;) {                \
            }                         \
        }                             \
    } while (0)

#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1

// The port's handlers take the CMSIS vector names (cube/app does not generate these three).
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
```

Create `stm32/firmware/app/app_main.h`:

```c
// App entry, called from USER CODE 2 of cube/app/Src/main.c after HAL_Init and the CubeMX init calls.
#pragma once

#define APP_VERSION "0.2.0"

void app_main(void);  // never returns
```

Create `stm32/firmware/app/app_main.c`:

```c
// Plan 02 app (spec §6.1, §6.3): board_early, then the wdog and init tasks and the scheduler.
// init reports the boot and then serves as the UART7 bench console: 'i' info, 'r' software reset,
// 'w' stop the watchdog kicks, 'c' CRC-32 of the image. Later plans replace it.
#include "app_main.h"
#include <stdbool.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "blog.h"
#include "board.h"
#include "crc32.h"
#include "main.h"
#include "now_us.h"
#include "startup_app.h"
#include "usart.h"
#include "wdog.h"

#define WDOG_STACK_WORDS 512   // 2 KB (spec §6.1)
#define INIT_STACK_WORDS 1536  // 6 KB

static StaticTask_t s_wdog_tcb, s_init_tcb;
static StackType_t s_wdog_stack[WDOG_STACK_WORDS], s_init_stack[INIT_STACK_WORDS];

extern const uint8_t __image_start__[], __image_end__[];  // app.ld: the whole QSPI image

static void report_boot(void)
{
    uint32_t rsr = RCC->RSR;
    RCC->RSR |= RCC_RSR_RMVF;  // the next boot reports only its own cause
    blog("\r\nSBIOT-H750 app " APP_VERSION " rsr=%08lx sysclk=%lu\r\n", (unsigned long)rsr,
         (unsigned long)SystemCoreClock);
    blog("app: handoff %s (%02lx), mpu %s\r\n", g_handoff_faults ? "FAULT" : "ok", (unsigned long)g_handoff_faults,
         g_mpu_boot_match ? "regions 0-1 match boot" : "MISMATCH with boot");
    blog("app: itcm vTaskSwitchContext=%p memcpy=%p\r\n", (void *)vTaskSwitchContext, (void *)memcpy);
}

static void info(void)
{
    // newlib-nano printf has no %llu: report milliseconds, which fit 32 bits for 49 days.
    unsigned long ms = (unsigned long)(now_us() / 1000u);
    blog("app: uptime_ms=%lu ticks=%lu wdog_max_gap_us=%lu\r\n", ms, (unsigned long)xTaskGetTickCount(),
         (unsigned long)wdog_max_gap_us());
}

static void init_task(void *arg)
{
    report_boot();
    info();
    for (;;) {
        uint8_t c;
        if (HAL_UART_Receive(&huart7, &c, 1, 50) != HAL_OK)
            continue;
        switch (c) {
        case 'i':
            info();
            break;
        case 'r':
            blog("app: software reset\r\n");
            vTaskDelay(pdMS_TO_TICKS(20));
            NVIC_SystemReset();
            break;
        case 'w':
            wdog_stop();
            blog("app: watchdog kicks stopped at %lu ms\r\n", (unsigned long)(now_us() / 1000u));
            break;
        case 'c': {
            size_t len = (size_t)(__image_end__ - __image_start__);
            uint64_t t0 = now_us();
            uint32_t crc = crc32_update(0, __image_start__, len);
            blog("app: image crc32=%08lx len=%lu in %lu us\r\n", (unsigned long)crc, (unsigned long)len,
                 (unsigned long)(now_us() - t0));
            break;
        }
        default:
            break;
        }
    }
}

void app_main(void)
{
    board_latch_zero();  // MX_GPIO_Init (app.ioc) left the latch outputs disabled
    board_kick();
    blog_init();
    xTaskCreateStatic(wdog_task, "wdog", WDOG_STACK_WORDS, NULL, 31, s_wdog_stack, &s_wdog_tcb);
    xTaskCreateStatic(init_task, "init", INIT_STACK_WORDS, NULL, 7, s_init_stack, &s_init_tcb);
    vTaskStartScheduler();
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    configASSERT(0);
}
```

- [ ] **Step 7: USER CODE hooks of the app**

In `stm32/firmware/cube/app/Src/main.c`, insert between the USER CODE markers, and nowhere else:
- `/* USER CODE BEGIN Includes */` … `END Includes`:
  ```c
  #include "app_main.h"
  #include "usart.h"  // CubeMX omits it while USART1-3 have no generated call
  ```
- `/* USER CODE BEGIN 2 */` … `END 2` (after `MX_UART7_Init();`): `  app_main();`

In `stm32/firmware/cube/app/Src/tim.c`, `/* USER CODE BEGIN TIM2_Init 2 */` … `END TIM2_Init 2`:
```c
  HAL_TIM_Base_Start(&htim2);  // now_us() and HAL_GetTick() count from here
```

Run: `stm32/firmware/tools/cubegen.sh && grep -c 'app_main();\|#include "app_main.h"\|#include "usart.h"  //' stm32/firmware/cube/app/Src/main.c && grep -c "HAL_TIM_Base_Start(&htim2)" stm32/firmware/cube/app/Src/tim.c`
Expected: two `ok` lines from cubegen, then `3` and `1`.

- [ ] **Step 8: Append the app target to the CMake tree**

Append to `stm32/firmware/CMakeLists.txt`:

```cmake

# ---- app: CubeMX init code for app.ioc (not the clock or QSPI parts), FreeRTOS, app/ ----
hal_sources(APP_HAL ${HAL_APP})
set(APP_CUBE
  ${CUBE}/app/Src/main.c ${CUBE}/app/Src/gpio.c ${CUBE}/app/Src/tim.c ${CUBE}/app/Src/usart.c
  ${CUBE}/app/Src/memorymap.c ${CUBE}/app/Src/stm32h7xx_it.c ${CUBE}/app/Src/stm32h7xx_hal_msp.c
  ${SYSTEM_C}
  ${CUBE}/app/startup_stm32h750xx.s)  # vector table only: its weak Reset_Handler is replaced
set(APP_RTOS
  ${RTOS}/tasks.c ${RTOS}/queue.c ${RTOS}/list.c ${RTOS}/timers.c
  ${RTOS}/portable/GCC/ARM_CM4F/port.c)
set(APP_OWN
  app/startup_app.c app/mpu_app.c app/app_main.c app/hal_tick.c app/image_header.c
  ${BOARD}/board_io.c ${BOARD}/now_us.c ${BOARD}/tick64.c ${BOARD}/wdog.c ${BOARD}/blog.c ${BOARD}/crc32.c
  ${BOARD}/syscalls.c)
add_executable(app ${APP_CUBE} ${APP_HAL} ${APP_RTOS} ${APP_OWN})
image_includes(app ${CUBE}/app)
target_include_directories(app PRIVATE app ${RTOS}/include ${RTOS}/portable/GCC/ARM_CM4F)
set_source_files_properties(${APP_OWN} TARGET_DIRECTORY app PROPERTIES COMPILE_OPTIONS "${STRICT}")
# The copy loops of Reset_Handler must stay loops: memcpy/memset run from ITCM, which they fill.
set_property(SOURCE app/startup_app.c TARGET_DIRECTORY app APPEND PROPERTY COMPILE_OPTIONS -fno-tree-loop-distribute-patterns)
target_compile_options(app PRIVATE -O2)
target_link_options(app PRIVATE -T${CMAKE_SOURCE_DIR}/app/app.ld -Wl,-Map=${CMAKE_BINARY_DIR}/app.map)
set_target_properties(app PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/app/app.ld)
bin_from_elf(app)
```

The app does not build `cube/app/Src/system_stm32h7xx.c` (B3), `syscalls.c`/`sysmem.c` (it has its own), or the peripherals of later plans (`adc.c`, `eth.c`, `spi.c`, `quadspi.c`).

- [ ] **Step 9: Build and run the gate**

Run: `cd stm32/firmware && cmake --preset h750 > /dev/null && cmake --build --preset app 2>&1 | grep -E "warning|error|QSPI:|ITCM:|DTCM:"; cd - > /dev/null; python3 stm32/firmware/tools/check_elf.py stm32/firmware/build/h750`
Expected:
- No `warning` or `error` lines.
- Memory use: `QSPI: ~27 KB`, `ITCM: ~7.6 KB`, `DTCM: ~13 KB`.
- 27 `ok` lines ending in `elf: ok`. Among them: `vTaskSwitchContext in ITCM (0x00000…)`, `memcpy in ITCM`, and `Reset_Handler calls no memcpy/memset (calls: SystemCoreClockUpdate, __libc_init_array, main, mpu_app_setup)`.

- [ ] **Step 10: The gate must catch the loop-to-memset rewrite (Review Focus 3)**

At `-O3` GCC turns the zero loop into `memset`, reached through `__memset_veneer`.

```bash
F=stm32/firmware; cp $F/CMakeLists.txt /tmp/claude-1000/cml.bak
sed -i 's/APPEND PROPERTY COMPILE_OPTIONS -fno-tree-loop-distribute-patterns/APPEND PROPERTY COMPILE_OPTIONS -O3 -ftree-loop-distribute-patterns/' $F/CMakeLists.txt
(cd $F && cmake --build --preset app > /dev/null 2>&1); python3 $F/tools/check_elf.py $F/build/h750 | grep -E "FAIL|elf:"
cp /tmp/claude-1000/cml.bak $F/CMakeLists.txt && touch $F/CMakeLists.txt
(cd $F && cmake --build --preset app > /dev/null 2>&1); python3 $F/tools/check_elf.py $F/build/h750 | tail -1
```

Expected: `FAIL app: Reset_Handler calls no memcpy/memset (calls: …__memset_veneer…)` and `elf: 1 check(s) failed`, then `elf: ok` after the restore. The `touch` is needed: `cp` keeps the old modification time and CMake would not reconfigure.

- [ ] **Step 11: The app on the board**

Run: `stm32/firmware/tools/flash.sh app && python3 stm32/firmware/test/bench/board.py 5`
Expected:
- `flash: app.bin (… bytes) at 0x90000000 verified`.
- One boot, logged as:
  ```
    SBIOT-H750 boot 0.2.0 rsr=00460000 sysclk=480000000
    boot: qspi id c84016
    boot: jump msp 20020000 pc 9000xxxx
    SBIOT-H750 app 0.2.0 rsr=00460000 sysclk=480000000
    app: handoff ok (00), mpu regions 0-1 match boot
    app: itcm vTaskSwitchContext=0x… memcpy=0x…
    app: uptime_ms=… ticks=… wdog_max_gap_us=0
  ```
  The two `itcm` addresses are below 0x10000.

- [ ] **Step 12: Commit**

```bash
git add stm32/firmware/app stm32/firmware/components/board stm32/firmware/CMakeLists.txt \
        stm32/firmware/cube/app/Src/main.c stm32/firmware/cube/app/Src/tim.c stm32/firmware/tools/check_elf.py
git commit -m "stm32 app: XIP Reset_Handler (FPU, SRAM clocks, copy tables, MPU once, caches), ITCM kernel, wdog, now_us, UART7 log, ELF gate"
```

### Task 6: Bench check of cold and warm boots, debugger halt, and the README

**Files:**
- Create: `stm32/firmware/test/bench/boot_check.py`
- Create: `stm32/firmware/tools/openocd/sbiot-h750.cfg`
- Modify: `stm32/README.md` (Commands and Status)

**Interfaces:**
- Consumes:
  - from Task 4: `board.listen`, `board.hard_reset`, `flash.sh`, and the boot log lines;
  - from Task 5: the app log lines and the console commands `i`, `c`, `r`, `w`.
- Produces: `python3 stm32/firmware/test/bench/boot_check.py BUILD_DIR [--cold]`, which prints `bench: ok`.

- [ ] **Step 1: Write the bench check**

Create `stm32/firmware/test/bench/boot_check.py`:

```python
#!/usr/bin/env python3
"""Plan 02 bench check: cold and warm boots of the bootloader and the XIP app (spec §6.2, §11.4).

    python3 stm32/firmware/test/bench/boot_check.py BUILD_DIR [--cold]

Needs the board flashed with BUILD_DIR's boot.bin and app.bin (tools/flash.sh both). With --cold it
waits up to 60 s for the user to power-cycle the board instead of running the warm-boot cases.
Prints one line per check and "bench: ok"; exits 1 on any failure.
"""
import os
import re
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board  # noqa: E402

RSR_PIN, RSR_POR, RSR_SFT, RSR_IWDG1 = 1 << 22, 1 << 23, 1 << 24, 1 << 26
failures = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        failures.append(what)


def boots(lines):
    """[(ms, app_rsr)] for each complete boot: boot banner, QSPI id, jump, app banner, handoff, ITCM."""
    out = []
    want = None
    for ms, line in lines:
        if line.startswith('SBIOT-H750 boot '):
            want = ['boot: qspi id c84016', 'boot: jump msp 20020000', 'SBIOT-H750 app ',
                    'app: handoff ok (00), mpu regions 0-1 match boot', 'app: itcm vTaskSwitchContext=0x']
            start, rsr = ms, None
        elif want and line.startswith(want[0]):
            if want[0] == 'SBIOT-H750 app ':
                rsr = int(re.search(r'rsr=([0-9a-f]{8})', line).group(1), 16)
            want.pop(0)
            if not want:
                out.append((start, rsr))
                want = None
    return out


def infos(lines):
    out = []
    for _, line in lines:
        m = re.match(r'app: uptime_ms=(\d+) ticks=(\d+) wdog_max_gap_us=(\d+)', line)
        if m:
            out.append(tuple(int(x) for x in m.groups()))
    return out


def main():
    build = sys.argv[1]
    app_bin = open(os.path.join(build, 'app.bin'), 'rb').read()
    if '--cold' in sys.argv:
        print('power-cycle the board now (60 s)...', flush=True)
        b = boots(board.listen(60))
        check(len(b) >= 1, 'cold boot reaches the app')
        check(bool(b) and bool(b[0][1] & RSR_POR), 'cold boot: PORRSTF set (rsr=%08x)' % (b[0][1] if b else 0))
    else:
        b = boots(board.listen(5, during=board.hard_reset))
        check(len(b) == 1 and bool(b[0][1] & RSR_PIN), 'pin reset: one complete boot with PINRSTF')

        lines = board.listen(4.5, sends=[(3.0, 'i'), (3.5, 'c')])
        i = infos(lines)
        check(len(i) == 1 and abs(i[0][0] - i[0][1]) <= 2, 'now_us (TIM2) agrees with the FreeRTOS tick: %s' % i)
        check(len(i) == 1 and 199000 <= i[0][2] <= 201000, 'wdog kicks every 200 ms')
        crc = [l for _, l in lines if l.startswith('app: image crc32=')]
        want = 'app: image crc32=%08x len=%d' % (zlib.crc32(app_bin), len(app_bin))
        check(len(crc) == 1 and crc[0].startswith(want), 'XIP image reads back as app.bin (%s)' % (crc[0] if crc else '-'))

        resets = 10
        b = boots(board.listen(1.0 + resets, sends=[(0.5 + n, 'r') for n in range(resets)]))
        check(len(b) == resets and all(r & RSR_SFT for _, r in b),
              '%d software resets: %d complete boots with SFTRSTF' % (resets, len(b)))

        for n in range(3):
            lines = board.listen(4, sends=[(0.5, 'w')])
            stop = [ms for ms, l in lines if l.startswith('app: watchdog kicks stopped')]
            b = boots(lines)
            dt = b[0][0] - stop[0] if stop and b else -1
            check(len(b) == 1 and bool(b[0][1] & RSR_IWDG1) and 1300 <= dt <= 2000,
                  'IWDG1 reset %d: %d ms after the last kick, IWDG1RSTF set' % (n + 1, dt))
    print('bench: ok' if not failures else 'bench: %d failure(s)' % len(failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
```

- [ ] **Step 2: Run the warm-boot matrix**

Run: `python3 stm32/firmware/test/bench/boot_check.py stm32/firmware/build/h750`
Expected:
```
ok   pin reset: one complete boot with PINRSTF
ok   now_us (TIM2) agrees with the FreeRTOS tick: [(N, N, 200000)]
ok   wdog kicks every 200 ms
ok   XIP image reads back as app.bin (app: image crc32=… len=… in … us)
ok   10 software resets: 10 complete boots with SFTRSTF
ok   IWDG1 reset 1: ~1600 ms after the last kick, IWDG1RSTF set
ok   IWDG1 reset 2: …
ok   IWDG1 reset 3: …
bench: ok
```

- [ ] **Step 3: A debugger halt does not trip IWDG1 (Review Focus 4)**

Create `stm32/firmware/tools/openocd/sbiot-h750.cfg`:

```tcl
# OpenOCD for debugging the SBIOT-H750 board through the ST-Link (spec §12.4). Flashing goes through
# tools/flash.sh: stmqspi cannot read this board's flash, so no QSPI flash bank is declared.
#   openocd -f stm32/firmware/tools/openocd/sbiot-h750.cfg      (Windows build, from WSL: see README)
source [find interface/stlink.cfg]
transport select swd
set CHIPNAME stm32h750vbt6
set QUADSPI 0
source [find target/stm32h7x.cfg]
reset_config none
```

Halt the running app for 5 s, resume it, then ask it for its uptime:

```bash
O=/mnt/c/Users/linh_bui/opt/xpack-openocd-0.12.0-7
(cd /mnt/c && $O/bin/openocd.exe -s "$(wslpath -w $O/openocd/scripts)" \
  -f "$(wslpath -w $PWD/stm32/firmware/tools/openocd/sbiot-h750.cfg)" \
  -c "init; halt; sleep 5000; resume; sleep 300; echo \"rsr=[read_memory 0x58024530 32 1]\"; shutdown" 2>&1 | grep rsr=)
python3 -c "
import sys; sys.path.insert(0, 'stm32/firmware/test/bench'); import board
for ms, l in board.listen(2.5, sends=[(1.5, 'i')]): print(l)"
```

Expected:
- `rsr=0x0`, because the app cleared RSR at boot and nothing has reset since.
- An `app: uptime_ms=…` line whose uptime exceeds the RTOS tick by about 5 000. TIM2 keeps counting while the core is halted; the tick stops.
- `wdog_max_gap_us` near 5 200 000.
- No `SBIOT-H750 boot` line.

- [ ] **Step 4: Cold boot (needs the user once)**

Ask the user, in one short message, to remove and restore the board's power once during the next 60 s. Then run:

Run: `python3 stm32/firmware/test/bench/boot_check.py stm32/firmware/build/h750 --cold`
Expected: `ok   cold boot reaches the app`, `ok   cold boot: PORRSTF set (rsr=…)`, `bench: ok`.

- [ ] **Step 5: BOR level 2 (spec §11.4)**

Option byte BOR_LEV = 2 (reset below about 2.7 V; the board runs at 3.34 V). The value is kept across power cycles and can be changed back over SWD.

```bash
CLI="/mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"
(cd /mnt/c && "$CLI" -c port=SWD mode=UR -ob BOR_LEV=2 2>&1 | grep -iE "error|success")
(cd /mnt/c && "$CLI" -c port=SWD mode=UR -ob displ 2>&1 | grep -i "BOR_LEV")
python3 stm32/firmware/test/bench/board.py 4 | grep -E "SBIOT-H750 (boot|app)"
```

Expected:
- the first command reports success;
- `BOR_LEV : 0x2` (CubeProgrammer may label the value as level 2);
- the board still boots to `SBIOT-H750 app`.

- [ ] **Step 6: Document the board commands**

In `stm32/README.md`, append to the `## Commands` code block:

```sh
source ~/opt/xpack/env.sh                                   # arm-none-eabi-gcc 15.2.1 on PATH
firmware/tools/cubegen.sh                                   # regenerate cube/ after an .ioc change (CubeMX headless)
python3 firmware/tools/check_pins.py                        # .ioc files against spec §3 and the CubeMX database
(cd firmware && cmake --preset h750 && cmake --build --preset boot && cmake --build --preset app)
python3 firmware/tools/check_elf.py firmware/build/h750     # layout and "app never touches clocks/QSPI/MPU"
firmware/tools/flash.sh both                                # SWD: boot.bin to 0x08000000, app.bin to QSPI
python3 firmware/test/bench/boot_check.py firmware/build/h750   # resets, watchdog, XIP CRC on the board
```

Replace the `## Status` paragraph with:

```markdown
Plans 01, 02 and 03 are done:
- **Plan 01:** the hardware-free core and the web UI copy, with host tests.
- **Plan 02:** the board boots our bootloader from internal flash and runs the FreeRTOS app in place from QSPI bank 2 at 80 MHz.
- **Plan 03:** the Edge node-by-node pipeline and the JSON arena.

See `docs/plans/2026-10-14-00-roadmap.md`.

Bench notes:
- The board's UART7 log is on the ST-Link VCP at COM4, 115200 baud.
- R20 (SGM706 WDO→MR) is not fitted on the current board, so IWDG1 is the reset source.
- QSPI is flashed with STM32CubeProgrammer and the team's external loader, because OpenOCD `stmqspi` cannot read this board's flash. OpenOCD is used for debugging only.
```

- [ ] **Step 7: Run every gate once more, then commit**

```bash
make -C stm32/firmware/test/host > /tmp/claude-1000/host.log 2>&1; echo host=$?
python3 stm32/firmware/tools/check_pins.py | tail -1 && python3 stm32/firmware/tools/test_check_pins.py | tail -1
python3 stm32/firmware/tools/check_elf.py stm32/firmware/build/h750 | tail -1
python3 stm32/firmware/tools/check_schema.py | tail -1
sha256sum -c /home/linh/projects/SBIOT-Gateway/.git/esp-tree-before.sha256 --quiet && echo esp-untouched
git add stm32/firmware/test/bench/boot_check.py stm32/firmware/tools/openocd/sbiot-h750.cfg stm32/README.md
git commit -m "stm32 bench: boot_check (pin, software, IWDG1 resets, XIP CRC, TIM2 vs tick, cold boot), OpenOCD debug config, README"
```

Expected: `host=0`, `pins: ok`, `test_check_pins: ok`, `elf: ok`, `consistent`, `esp-untouched`.

## Plan 02 done when

- `make -C stm32/firmware/test/host` passes, with `test_board` included.
- `check_pins.py`, `test_check_pins.py` and `check_elf.py` print `ok`, and the `-O3` mutant of Task 5 fails `check_elf.py`.
- `cubegen.sh` run twice in a row leaves `git status stm32/firmware/cube` clean.
- On the board:
  - **Task 4, Step 10:** with a blank QSPI the bootloader refuses to jump and does not reset.
  - **Task 6, Steps 2–5:** `boot_check.py` prints `bench: ok` for warm and cold boots; a 5 s debugger halt causes no reset; BOR_LEV is 2.
- The ESP32 tree is untouched (`.git/esp-tree-before.sha256`).
- Codex approves the plan before execution and the branch after it. Then a PR goes to SBIOT-H750 with STM32 files only.
