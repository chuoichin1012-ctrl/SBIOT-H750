# SBIOT-H750 (STM32H750VBT6)

Firmware and web UI for the SBIOT gateway on the STM32H750 board. The firmware runs FreeRTOS, lwIP and mbedTLS, with Mongoose for the web server and coreMQTT for MQTT. It is a port of the ESP32 SBIOT-GW100 firmware, and the two code bases are developed separately.

| Directory | Contents |
|---|---|
| `docs/specs/` | The design, approved by Codex. Start here |
| `docs/plans/` | The roadmap (11 sub-plans) and the implementation plans |
| `docs/review/` | Codex reviews and how each finding was handled |
| `firmware/` | Firmware sources. `components/` holds the modules, `third_party/` the vendored libraries, `test/host/` the host unit tests |
| `web/` | The web UI embedded in the firmware (Vite, Preact, TypeScript, Tailwind) |

The documents in `docs/` were written inside the parent repository, so their paths start with `stm32/`. They also refer to the board schematic (`docs/Job2.PDF`) and to the ESP32 firmware (`firmware/` at the parent's root), which live only in the parent repository.

## Commands

Run these from this directory:

```sh
make -C firmware/test/host              # host unit tests (ASan/UBSan)
python3 firmware/tools/check_schema.py  # web UI field ids against the firmware schema
npm --prefix web ci                     # once
npm --prefix web run build              # type-check and build the UI
npm --prefix web run test:e2e           # UI regression tests (headless Chromium; set CHROME_PATH if needed)
npm --prefix web run build:firmware     # embed the UI into firmware/components/webapi/www
source ~/opt/xpack/env.sh                                   # arm-none-eabi-gcc 15.2.1 on PATH
firmware/tools/cubegen.sh                                   # regenerate cube/ after an .ioc change (CubeMX headless)
python3 firmware/tools/check_pins.py                        # .ioc files against spec §3 and the CubeMX database
(cd firmware && cmake --preset h750 && cmake --build --preset boot && cmake --build --preset app)
python3 firmware/tools/check_elf.py firmware/build/h750     # layout and "app never touches clocks/QSPI/MPU"
firmware/tools/flash.sh both                                # SWD: boot.bin to 0x08000000, app.bin to QSPI
python3 firmware/test/bench/boot_check.py firmware/build/h750   # resets, watchdog, XIP CRC on the board (--hostile, --cold)
```

## Status

Plans 01, 02 and 03 are done:
- **Plan 01:** the hardware-free core and the web UI copy, with host tests.
- **Plan 02:** the board boots our bootloader from internal flash and runs the FreeRTOS app in place from QSPI bank 2 at 80 MHz.
- **Plan 03:** the Edge node-by-node pipeline and the JSON arena.

See `docs/plans/2026-10-14-00-roadmap.md`.

Bench notes:
- The board's UART7 log is on the ST-Link VCP at COM4, 115200 baud.
- R20 (SGM706 WDO→MR) is not fitted on the current board, so IWDG1 is the reset source.
- QSPI is flashed with STM32CubeProgrammer and the team's external loader, because OpenOCD `stmqspi` cannot read this board's flash. OpenOCD is used for debugging only.
