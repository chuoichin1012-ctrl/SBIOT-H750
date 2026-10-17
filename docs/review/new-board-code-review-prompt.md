# Code review: `stm32/firmware/tools/new_board.sh` (branch `new-board`)

The user asked for one script that programs a new board. It wraps the existing tools in the production order of spec §12.4:
1. Build the web UI and the firmware (skipped with `--no-build`).
2. Check the ST-Link probe.
3. Mass-erase, only with `--erase`.
4. `flash.sh bind KEY` first, with `--sn` or the SN counter.
5. `flash.sh both`.
6. Check the first boot on the UART7 console (`board.listen` during an SWD hard reset): `bind: ok` and the app's banner.
7. RDP Level 1 only with `--rdp` and a typed "RDP".

The script also refuses a dev key with a production build (`SBIOT_BIND_PUBKEY_FILE` set in the CMake cache), and a production key with a dev build.

Please check:
- the order against spec §12.4;
- error handling under `set -euo pipefail` (the `grep` / `tee` pipelines, a failed bind, no probe);
- that nothing destructive runs without its flag;
- that the summary line is right.

Findings as P1 / P2 / minor with file:line. End with **APPROVED** or **CHANGES NEEDED**.
