# SBIOT-H750 firmware images (spec §4, §12.4): boot.elf (internal flash) and app.elf/app.bin (QSPI XIP).
#   cmake --preset h750 && cmake --build --preset boot && cmake --build --preset app
cmake_minimum_required(VERSION 3.22)
project(sbiot_h750 C ASM)

set(CUBE ${CMAKE_SOURCE_DIR}/cube)
set(HAL ${CMAKE_SOURCE_DIR}/third_party/stm32cube_h7/Drivers)
set(RTOS ${CMAKE_SOURCE_DIR}/third_party/FreeRTOS-Kernel)
set(BOARD ${CMAKE_SOURCE_DIR}/components/board)
# FW pack 1.12.0's system_stm32h7xx.c has a wrong D1CorePrescTable (APB /2 read as /1, so every
# peripheral clock and baud rate is off by 2); both images use the fixed cmsis-device-h7 v1.10.7 one.
set(SYSTEM_C ${HAL}/CMSIS/Device/ST/STM32H7xx/Source/Templates/system_stm32h7xx.c)

set(CPU -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb)
add_compile_options(${CPU} -ffunction-sections -fdata-sections -g3)
add_compile_definitions(STM32H750xx USE_HAL_DRIVER USE_PWR_LDO_SUPPLY)
add_link_options(${CPU} --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage
                 -Wl,--no-warn-rwx-segments)
# Our own code is warning-free; vendored and generated code builds with the defaults.
set(STRICT -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror)

# HAL modules per image: the ones CubeMX copies for each .ioc.
set(HAL_COMMON cortex rcc rcc_ex flash flash_ex gpio hsem dma dma_ex mdma pwr pwr_ex exti i2c i2c_ex qspi uart uart_ex)
set(HAL_BOOT ${HAL_COMMON} iwdg)
set(HAL_APP ${HAL_COMMON} adc adc_ex eth eth_ex spi spi_ex tim tim_ex)

function(hal_sources out)
  set(list ${HAL}/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c)
  foreach(m ${ARGN})
    list(APPEND list ${HAL}/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_${m}.c)
  endforeach()
  set(${out} ${list} PARENT_SCOPE)
endfunction()

function(image_includes target cube_dir)
  target_include_directories(${target} PRIVATE
    ${cube_dir}/Inc
    ${HAL}/STM32H7xx_HAL_Driver/Inc
    ${HAL}/STM32H7xx_HAL_Driver/Inc/Legacy
    ${HAL}/CMSIS/Device/ST/STM32H7xx/Include
    ${HAL}/CMSIS/Include
    ${BOARD}/include)
endfunction()

function(bin_from_elf target)
  set_target_properties(${target} PROPERTIES SUFFIX .elf)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> ${CMAKE_BINARY_DIR}/${target}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>)
endfunction()

# ---- bootloader: everything CubeMX generates for boot.ioc, plus boot/ ----
file(GLOB BOOT_CUBE ${CUBE}/boot/Src/*.c)
list(REMOVE_ITEM BOOT_CUBE ${CUBE}/boot/Src/system_stm32h7xx.c)
list(APPEND BOOT_CUBE ${SYSTEM_C})
hal_sources(BOOT_HAL ${HAL_BOOT})
set(BOOT_OWN boot/boot.c boot/qspi_flash.c boot/jumpcheck.c ${BOARD}/board_io.c)
add_executable(boot ${BOOT_CUBE} ${CUBE}/boot/startup_stm32h750xx.s ${BOOT_HAL} ${BOOT_OWN})
image_includes(boot ${CUBE}/boot)
target_include_directories(boot PRIVATE boot)
set_source_files_properties(${BOOT_OWN} TARGET_DIRECTORY boot PROPERTIES COMPILE_OPTIONS "${STRICT}")
target_compile_options(boot PRIVATE -Os)
target_link_options(boot PRIVATE -T${CUBE}/boot/stm32h750vbtx_flash.ld -Wl,-Map=${CMAKE_BINARY_DIR}/boot.map)
set_target_properties(boot PROPERTIES LINK_DEPENDS ${CUBE}/boot/stm32h750vbtx_flash.ld)
bin_from_elf(boot)
