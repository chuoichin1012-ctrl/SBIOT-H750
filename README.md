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
```

## Status

Plan 01 is done: the hardware-free core and the web UI copy, with host tests. Bring-up on the board starts with Plan 02. See `docs/plans/2026-10-14-00-roadmap.md`.
