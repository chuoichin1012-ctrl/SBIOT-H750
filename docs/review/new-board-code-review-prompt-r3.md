# Code review r3: `tools/new_board.sh`, branch `new-board`

Your r2 P2 is addressed in the last commit (`git show HEAD`):
- `new_board_check.load_board()` puts `<tools>/../test/bench` (derived from `__file__`) on `sys.path` and imports `board`.
- `new_board.sh` runs `python3 "$tools/new_board_check.py"` with no `cd` and no `PYTHONPATH`.
- Test: `Launch.test_board_module_found_from_tools`. Run with discovery from `/tmp`, it imports `board` and finds `listen` / `hard_reset`; before the fix it raised `ModuleNotFoundError`.

Findings as P1 / P2 / minor with file:line. End with **APPROVED** or **CHANGES NEEDED**.
