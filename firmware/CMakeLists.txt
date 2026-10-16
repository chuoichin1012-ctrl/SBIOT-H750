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
set(HAL_BOOT ${HAL_COMMON} iwdg spi spi_ex)
set(HAL_APP ${HAL_COMMON} adc adc_ex eth eth_ex rng rng_ex spi spi_ex tim tim_ex)

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
# Anti-clone public key (spec §11.5): the 32-byte Ed25519 public key of the production signing key
# (tools/provision.py pubkey), embedded in the bootloader and the app as gen/bind_key.h. Without
# it: the development key, which only boards bound with "provision.py ... dev" accept.
set(SBIOT_BIND_PUBKEY_FILE "" CACHE FILEPATH "32-byte Ed25519 public key (spec 11.5); empty: dev key")
if(SBIOT_BIND_PUBKEY_FILE)
  file(SIZE ${SBIOT_BIND_PUBKEY_FILE} BIND_PUB_SIZE)
  if(NOT BIND_PUB_SIZE EQUAL 32)
    message(FATAL_ERROR "SBIOT_BIND_PUBKEY_FILE must hold exactly 32 bytes, ${SBIOT_BIND_PUBKEY_FILE} has ${BIND_PUB_SIZE}")
  endif()
  file(READ ${SBIOT_BIND_PUBKEY_FILE} BIND_PUB_HEX HEX)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SBIOT_BIND_PUBKEY_FILE})
  set(BIND_DEV 0)
else()
  message(WARNING "SBIOT_BIND_PUBKEY_FILE is not set: the anti-clone DEV key is built in (not for production)")
  set(BIND_PUB_HEX 2a140afad3c3e566a000ad19a5b526b39375b2ab0381391b8bf329b33a89b3cf)  # provision.py DEV_PUBLIC_HEX
  set(BIND_DEV 1)
endif()
string(REGEX REPLACE "(..)" "0x\\1," BIND_PUB_BYTES ${BIND_PUB_HEX})
configure_file(components/ota/bind_key.h.in ${CMAKE_BINARY_DIR}/gen/bind_key.h @ONLY)

# OTA and anti-clone logic shared with the host tests (components/ota), and Monocypher's Ed25519 check.
set(OTA ${CMAKE_SOURCE_DIR}/components/ota)
set(STORE ${CMAKE_SOURCE_DIR}/components/store)
set(CFGM ${CMAKE_SOURCE_DIR}/components/cfgmodel)
set(NET ${CMAKE_SOURCE_DIR}/components/netmgr)
set(SYS ${CMAKE_SOURCE_DIR}/components/sysmgr)
set(WEB ${CMAKE_SOURCE_DIR}/components/webapi)
set(SER ${CMAKE_SOURCE_DIR}/components/serial)
set(IOM ${CMAKE_SOURCE_DIR}/components/iomgr)
set(SVC ${CMAKE_SOURCE_DIR}/components/services)
set(PROTO ${CMAKE_SOURCE_DIR}/components/proto)
set(MONO ${CMAKE_SOURCE_DIR}/third_party/monocypher)
set(OTA_BOOT ${OTA}/sha256.c ${OTA}/ota_image.c ${OTA}/bind.c)
set(MONO_SRC ${MONO}/monocypher.c ${MONO}/monocypher-ed25519.c)
set(BOOT_OWN boot/boot.c boot/qspi_flash.c boot/install.c ${BOARD}/board_io.c ${BOARD}/spi_nor.c ${BOARD}/crc32.c ${OTA_BOOT})
add_executable(boot ${BOOT_CUBE} ${CUBE}/boot/startup_stm32h750xx.s ${BOOT_HAL} ${BOOT_OWN} ${MONO_SRC})
image_includes(boot ${CUBE}/boot)
target_include_directories(boot PRIVATE boot ${OTA}/include ${MONO} ${CMAKE_BINARY_DIR}/gen)
set_source_files_properties(${BOOT_OWN} TARGET_DIRECTORY boot PROPERTIES COMPILE_OPTIONS "${STRICT}")
target_compile_options(boot PRIVATE -Os)
target_link_options(boot PRIVATE -T${CUBE}/boot/stm32h750vbtx_flash.ld -Wl,-Map=${CMAKE_BINARY_DIR}/boot.map)
set_target_properties(boot PROPERTIES LINK_DEPENDS ${CUBE}/boot/stm32h750vbtx_flash.ld)
bin_from_elf(boot)

# ---- app: CubeMX init code for app.ioc (not the clock or QSPI parts), FreeRTOS, app/ ----
hal_sources(APP_HAL ${HAL_APP})
set(APP_CUBE
  ${CUBE}/app/Src/main.c ${CUBE}/app/Src/gpio.c ${CUBE}/app/Src/tim.c ${CUBE}/app/Src/usart.c ${CUBE}/app/Src/spi.c ${CUBE}/app/Src/dma.c
  ${CUBE}/app/Src/memorymap.c ${CUBE}/app/Src/stm32h7xx_it.c ${CUBE}/app/Src/stm32h7xx_hal_msp.c
  ${CUBE}/app/Src/eth.c ${CUBE}/app/Src/rng.c  # MspInit, handle and descriptors only: MX_ETH_Init and MX_RNG_Init are never called
  ${CUBE}/app/Src/adc.c  # hadc3 and HAL_ADC_MspInit only: MX_ADC3_Init is never called (adc_drv.c sets ADC3 up)
  ${SYSTEM_C}
  ${CUBE}/app/startup_stm32h750xx.s)  # vector table only: its weak Reset_Handler is replaced
set(APP_RTOS
  ${RTOS}/tasks.c ${RTOS}/queue.c ${RTOS}/list.c ${RTOS}/timers.c ${RTOS}/stream_buffer.c
  ${RTOS}/portable/GCC/ARM_CM4F/port.c ${RTOS}/portable/MemMang/heap_5.c)
set(APP_OWN
  app/startup_app.c app/mpu_app.c app/app_main.c app/hal_tick.c app/image_header.c app/bench_ota.c app/bench_serial.c app/flash_spi4.c app/heap_newlib.c ${BOARD}/heap_wrap.c
  ${BOARD}/board_io.c ${BOARD}/now_us.c ${BOARD}/tick64.c ${BOARD}/wdog.c ${BOARD}/blog.c ${BOARD}/crc32.c
  ${BOARD}/syscalls.c ${BOARD}/spi_nor.c ${OTA}/sha256.c ${OTA}/ota_image.c ${OTA}/ota_app.c ${OTA}/bind.c
  app/store_port.c ${STORE}/store.c ${STORE}/fal_nor.c ${CMAKE_SOURCE_DIR}/components/syslog/syslog.c ${CFGM}/cfgmodel.c ${PROTO}/ipacl.c ${PROTO}/jsonscan.c
  ${BOARD}/board_rng.c ${BOARD}/rng_core.c ${NET}/ip101.c ${NET}/netcfg.c ${NET}/eth_drv.c ${NET}/netmgr.c ${NET}/lte.c ${NET}/lte_uart.c ${NET}/lte_power.c ${NET}/inet.c ${PROTO}/cmux.c ${PROTO}/at_parse.c app/fdset_check.c
  ${SYS}/clock.c ${SYS}/sysmgr.c ${WEB}/web_core.c ${WEB}/web.c ${WEB}/api.c ${WEB}/console.c ${WEB}/mg_port.c
  ${CMAKE_SOURCE_DIR}/components/arena/arena.c
  ${SER}/serial_frame.c ${SER}/uart_drv.c ${SER}/serial.c
  ${IOM}/io_core.c ${IOM}/adc_core.c ${IOM}/adc_drv.c ${IOM}/iomgr.c ${IOM}/io_mb.c ${SVC}/bridge_core.c ${SVC}/edge_core.c ${SVC}/edge_rt.c ${PROTO}/edgecore.c ${PROTO}/rwprot.c ${PROTO}/mqttutil.c ${SVC}/tls_core.c ${SVC}/tls_port.c ${SVC}/tlsconn.c ${SVC}/net.c ${SVC}/bridge.c ${SVC}/services.c ${SVC}/mqtt_transport.c ${SVC}/mqtt_outbox.c ${SVC}/mqtt_sess.c ${SVC}/mqttgw.c ${PROTO}/modbus.c ${SYS}/reload.c app/bench_io.c)
# The embedded UI (Plan 06b): tools/embed_web.py writes the gzip bytes as C arrays and their table.
set(APP_WWW ${WEB}/www/www_files.c)
if(NOT EXISTS ${APP_WWW})  # a build output (gitignored), not a source
  message(FATAL_ERROR "No embedded UI: run `npm ci && npm run build:firmware` in stm32/web first (spec §10.3).")
endif()
# Network (Plan 06a): lwIP 2.2.1 and its FreeRTOS port (third_party/lwip/README.md), built without -Werror.
set(LWIP ${CMAKE_SOURCE_DIR}/third_party/lwip)
file(GLOB LWIP_SRC ${LWIP}/src/core/*.c ${LWIP}/src/core/ipv4/*.c ${LWIP}/src/api/*.c)
list(APPEND LWIP_SRC ${LWIP}/src/netif/ethernet.c ${LWIP}/contrib/ports/freertos/sys_arch.c ${LWIP}/src/apps/sntp/sntp.c)
# PPPoS (Plan 10): every file of src/netif/ppp; the ones lwipopts.h turns off compile to nothing.
file(GLOB LWIP_PPP ${LWIP}/src/netif/ppp/*.c ${LWIP}/src/netif/ppp/polarssl/*.c)
list(APPEND LWIP_SRC ${LWIP_PPP})
# MQTT (Plan 09): coreMQTT 2.3.1 with the SBIOT patch (third_party/coremqtt/README.md).
set(CMQTT ${CMAKE_SOURCE_DIR}/third_party/coremqtt/source)
set(CMQTT_SRC ${CMQTT}/core_mqtt.c ${CMQTT}/core_mqtt_serializer.c ${CMQTT}/core_mqtt_state.c)
# TLS (Plan 08b): mbedTLS 3.6.7 LTS (third_party/mbedtls/README.md), its settings in tls_config.h.
set(MBED ${CMAKE_SOURCE_DIR}/third_party/mbedtls)
file(GLOB MBED_SRC ${MBED}/library/*.c)
# Storage (Plan 05b): FlashDB and cJSON are upstream code, built without our -Werror.
set(FDB ${CMAKE_SOURCE_DIR}/third_party/flashdb)
set(UPSTREAM_APP ${FDB}/src/fdb.c ${FDB}/src/fdb_kvdb.c ${FDB}/src/fdb_tsdb.c ${FDB}/src/fdb_utils.c
  ${FDB}/port/fal/src/fal.c ${FDB}/port/fal/src/fal_flash.c ${FDB}/port/fal/src/fal_partition.c
  ${CMAKE_SOURCE_DIR}/third_party/cjson/cJSON.c ${LWIP_SRC} ${CMAKE_SOURCE_DIR}/third_party/mongoose/mongoose.c
  ${MBED_SRC} ${CMQTT_SRC})
add_executable(app ${APP_CUBE} ${APP_HAL} ${APP_RTOS} ${APP_OWN} ${MONO_SRC} ${UPSTREAM_APP} ${APP_WWW})
image_includes(app ${CUBE}/app)
target_include_directories(app PRIVATE app ${RTOS}/include ${RTOS}/portable/GCC/ARM_CM4F ${OTA}/include ${MONO} ${CMAKE_BINARY_DIR}/gen
  ${CMAKE_SOURCE_DIR}/components/syslog/include ${STORE}/include ${STORE}/port ${FDB}/inc ${FDB}/port/fal/inc ${CFGM}/include ${PROTO}/include ${CMAKE_SOURCE_DIR}/third_party/cjson
  ${NET}/include ${LWIP}/src/include ${LWIP}/contrib/ports/freertos/include
  ${SYS}/include ${WEB}/include ${SER}/include ${IOM}/include ${SVC}/include ${CMAKE_SOURCE_DIR}/components/arena/include
  ${CMAKE_SOURCE_DIR}/third_party/mongoose ${MBED}/include ${CMQTT}/include ${CMQTT}/interface)
target_compile_definitions(app PRIVATE MBEDTLS_CONFIG_FILE="tls_config.h")
set_source_files_properties(${APP_OWN} TARGET_DIRECTORY app PROPERTIES COMPILE_OPTIONS "${STRICT}")
# The copy loops of Reset_Handler must stay loops: memcpy/memset run from ITCM, which they fill.
set_property(SOURCE app/startup_app.c TARGET_DIRECTORY app APPEND PROPERTY COMPILE_OPTIONS -fno-tree-loop-distribute-patterns)
target_compile_options(app PRIVATE -O2)
target_link_libraries(app PRIVATE m)  # edgecore's llround (Plan 08c); --gc-sections keeps only what is called
# newlib-nano's printf has no %f/%g unless asked: Edge's values (edge_format), cJSON's non-integer numbers and
# cfgmodel's range messages print them (Plan 08c bench: every report rendered empty numbers).
target_link_options(app PRIVATE -u _printf_float)
target_link_options(app PRIVATE -T${CMAKE_SOURCE_DIR}/app/app.ld -Wl,-Map=${CMAKE_BINARY_DIR}/app.map)
set_target_properties(app PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/app/app.ld)
bin_from_elf(app)
# The app's version, once: compiled in, and written into the image header of app.img (spec §11.1).
set(APP_VERSION 0.7.0 CACHE STRING "app version major.minor.patch")
target_compile_definitions(app PRIVATE APP_VERSION="${APP_VERSION}")
add_custom_command(TARGET app POST_BUILD
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/mkimage.py --version ${APP_VERSION} ${CMAKE_BINARY_DIR}/app.bin
          ${CMAKE_BINARY_DIR}/app.img
  COMMENT "mkimage: app.img ${APP_VERSION}")
