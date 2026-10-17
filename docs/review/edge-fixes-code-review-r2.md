- **P2 — `stm32/web/src/lib/edge.ts:428`: IO normalization still differs from the firmware.** A valid saved model `[{source:"io",nodes:[{ioPoint:"DO1"}]}]` runs as `SBIOT_IO/DO1`, because `cfgmodel.c` overrides the device and point names. The UI instead produces `Device1/Node1`, offering `Node1` in linkage choices. An event selecting it is skipped at boot because that point does not exist; existing events referencing `DO1` can also fail UI validation. Apply the firmware’s source-specific normalization using `fixedName` and `ioRegister`, and add this sparse-IO test.

The three r1 fixes are addressed. Focused read-only UI checks passed; no board tests were run.

**CHANGES NEEDED**