**CHANGES NEEDED.** The behavior is implementable, but four details need correction.

1. **Make the hold decision before `boot_early()` initializes GPIO.**  
   [boot.c:114](/home/linh/projects/SBIOT-Gateway/stm32/firmware/boot/boot.c:114) calls `MX_GPIO_Init()` before clock setup, and generated `main()` calls it again before `boot_main()`. The proposed decision in `boot_main()` is too late. On the hold path, **both bootloader GPIO calls and the app GPIO initialization must preserve LE/OE low**, without deliberately pulsing OE high. Update the generation/pin checks accordingly; do not rely on an unmeasured “few µs is harmless.”

2. **Specify how the record and physical latch stay consistent across an interrupted update.**  
   Currently [iomgr.c:46](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/iomgr/iomgr.c:46) skips writing when the requested mask equals `s_latched`. If reset occurs after changing the hardware but before publishing the new record, the old record can still have a valid CRC. Restoring `s_latched` from it can leave the physical output and software state disagreeing indefinitely.  
   A simple policy is: invalidate and clean the record **before** changing the latch, then publish and clean the completed record afterward. An interrupted update then takes the explicitly documented invalid-record/off fallback. Add tests for these update boundaries.

3. **Define the record’s startup lifecycle.**  
   On a non-hold boot, invalidate stale retained state before reset flags can be cleared. Once IO configuration is loaded, publish the current `keep`, actual latch mask and commanded mask—even when outputs remain zero and `latch()` would return early. Preserve the original reset flags for subsequent decisions.

4. **Strengthen the electrical acceptance test.**  
   ODR readings and watching the relay cannot demonstrate absence of a short interruption. Measure OE, LE and the relay drive/contact across software reset, watchdog reset and NRST, including holding NRST asserted. With R103 absent, firmware cannot guarantee OE’s level while the MCU remains in reset; document that hardware limitation.

**Do not leave OE/LE untouched after execution resumes.** On a validated hold path, preload both outputs low and configure them as outputs promptly. LE low preserves stored data; OE low enables it. OE high disconnects the outputs without clearing the latch. [74HC573 datasheet](https://assets.nexperia.com/documents/data-sheet/74HC_HCT573.pdf)

The mode changes and removal of flash persistence are otherwise reasonable. Test flip startup at a nonzero initialization time, and ensure `io_core_commanded()` returns the **current flip phase**, rather than the existing blink-enabled flag.