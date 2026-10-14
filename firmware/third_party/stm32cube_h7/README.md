# STM32H7 HAL and CMSIS (vendored, pinned)

| Part | Release | Commit |
|---|---|---|
| `Drivers/STM32H7xx_HAL_Driver` (Inc, Src) | STMicroelectronics/stm32h7xx-hal-driver v1.11.6 | a1996eed9172b59887bafaaa0ea1816ea14d48b5 |
| `Drivers/CMSIS/Device/ST/STM32H7xx` (stm32h7xx.h, stm32h750xx.h, system_stm32h7xx.h, Source/Templates/system_stm32h7xx.c) | STMicroelectronics/cmsis-device-h7 v1.10.7 | 8f922cdc7cc6de2344e75ddd657889f4ff761790 |
| `Drivers/CMSIS/Include` | STMicroelectronics/cmsis-core v5.9.0_20250520 | b7487de1303e2eb77c402432e368691e9dcfb5f0 |

Licences: BSD-3-Clause (HAL), Apache-2.0 (CMSIS); see the LICENSE.md next to each part.

Why not the copy CubeMX makes from FW pack 1.12.0: that pack's `system_stm32h7xx.c` has
`D1CorePrescTable = {0, 0, 0, 0, 0, 0, 0, 0, 1, ...}`, so the HAL reads an APB prescaler of 2 as 1 and every
peripheral clock and baud rate comes out twice too high. Both images build `Source/Templates/system_stm32h7xx.c`
from v1.10.7 instead of the generated one. CubeMX still writes `stm32h7xx_hal_conf.h` for each image.
