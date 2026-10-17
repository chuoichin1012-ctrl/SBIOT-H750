# Code review r3: branch `edge-fixes`

Your r2 P2 is addressed in the last commit (`git show HEAD`):
- `normalizeSlaves` applies cfgmodel's source rules after the item defaults.
- The IO device is named `SBIOT_IO`, and the virtual device `SBIOT_VIRTUAL` (`fixedName`).
- An IO point whose `ioPoint` is a valid channel takes its name, fc, reg and dtype from `ioRegister` (DO → FC1 bool, DI → FC2 bool, AI → FC4 float32 at 2×index), as `cfgmodel.c` does.
- Virtual points get FC 4.
- Unit test: the sparse IO model `[{source:"io",nodes:[{ioPoint:"DO1"},{ioPoint:"AI2"}]}]` plus a virtual device.

Findings as P1 / P2 / minor with file:line. End with **APPROVED** or **CHANGES NEEDED**.
