# SBIOT-H750 (STM32H750VBT6)

Firmware and web UI for the STM32H750 board (`docs/Job2.PDF`). This tree is separate from the ESP32 SBIOT-GW100 code in `firmware/` and `src/`, and the two are not kept in sync.

- `docs/specs/`: the design (approved by Codex). Start there.
- `docs/plans/`: the roadmap and the implementation plans.
- `docs/review/`: Codex reviews and how each finding was handled.
- `firmware/`: firmware sources. Host tests: `make -C stm32/firmware/test/host`.
- `web/`: the STM32 web UI (`npm --prefix stm32/web run build`).
