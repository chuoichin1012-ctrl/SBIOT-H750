- **P2 — `stm32/firmware/tools/new_board_check.py:25`: `import board` fails with the script’s normal invocation.** Running `python3 "$tools/new_board_check.py"` puts `tools/`, not the current `test/bench/` directory, on Python’s import path. Without an external `PYTHONPATH` workaround, provisioning stops after binding, before `flash.sh both`. Add `test/bench` to `sys.path` using a path derived from `__file__`. Add a launch-path test; the existing tests only exercise `judge()`.

The three judge tests and shell syntax check passed. No board operations were performed.

**CHANGES NEEDED**