# Code review r2: `tools/new_board.sh`, branch `new-board`

Your r1 (`stm32/docs/review/new-board-code-review.md`) is addressed in the last commit (`git show HEAD`):

- **P2 RDP:** after the typed confirmation, the script runs `-ob RDP=0xBB` and then reads the option bytes back (`-ob displ`). It fails unless RDP reads 0xBB.
- **P2 the boot check:** `tools/new_board_check.py`:
  - it runs `flash.sh both` with the UART7 console already open (`board.listen(90, during=...)`), so the first boot after it is read from its first line;
  - `judge()` requires each of these:
    - `boot: install` with `seeded-factory`, and without `installed-staging`, `restored-factory` or `SEED-FAILED` (`boot/install.c:118` seeds a blank factory slot; `test_install` covers it);
    - `bind: ok`;
    - the app banner;
    - `app: store mounted`;
    - no `halted`, `INVALID` or `FAILED` line;
  - unit tests in `tools/test_new_board_check.py`: a new board passes; a reworked board (no seed, or `installed-staging`) and failed starts are stopped.
- **P2 rework:**
  - `--erase` is dropped; the script is for new boards only.
  - A board whose first boot does not seed the factory (SPI4 already holds one) is stopped, with a pointer to spec §12.4's rework steps.
- **Minor:** bind's exit status is checked (`out=$(... flash.sh bind ...) || fail`), as well as its "bound to" line.

Findings as P1 / P2 / minor with file:line. End with **APPROVED** or **CHANGES NEEDED**.
