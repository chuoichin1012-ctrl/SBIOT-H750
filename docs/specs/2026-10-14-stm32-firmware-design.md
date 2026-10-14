# Firmware SBIOT Gateway trên STM32H750: thiết kế

**Ngày:** 2026-10-14
**Trạng thái:** spec chờ duyệt. Sau khi duyệt sẽ dùng làm đầu vào cho kế hoạch triển khai.
**Phần cứng:** board STM32H750VBT6, sơ đồ nguyên lý `docs/Job2.PDF`.
**Chuẩn hành vi:**
- firmware ESP32 hiện có (`firmware/`, mô tả trong `firmware/docs/ARCHITECTURE.md`);
- hợp đồng API của Web UI;
- tài liệu chức năng USR-M100 (`docs/USR-M100_Chuc_nang_chi_tiet.md`).

Các khác biệt so với chuẩn này được liệt kê đầy đủ ở §13.

---

## 1. Mục tiêu và tiêu chí thành công

Xây dựng firmware cho board STM32H750 với chức năng tương đương firmware ESP32 (SBIOT-GW100). Firmware mới khai thác đủ phần cứng của board: hai cổng nối tiếp, 2 AI, LTE EC600U.

Tiêu chí hoàn thành:
1. Web UI bản STM32 (`stm32/web`) dùng được mọi trang trên firmware thật: lưu rồi tải lại không mất dữ liệu.
2. Các bộ test bench (§12) đều đạt trên board thật: bridge, MQTT, Edge, misc, OTA.
3. Cắt điện ngẫu nhiên 20 lần trong lúc OTA đang cài, lần nào board cũng boot được, bằng image mới hoặc image cũ.
4. Đo trên board thật: stack còn dư ≥ 1 KB ở mọi task, heap của mỗi vùng còn dư ≥ 16 KB ở tải tối đa (§12.3).
5. `audit_keys.py` báo 0 key không có module nào đọc. `check_schema.py` không báo lệch giữa UI và schema.

## 2. Các quyết định đã chốt

| # | Chủ đề | Quyết định |
|---|---|---|
| D1 | RTOS và stack mạng | FreeRTOS + lwIP + mbedTLS 3.x |
| D2 | Tổ chức mã | Cây `stm32/` **tách riêng hoàn toàn** khỏi bản ESP32: firmware và cả Web UI. Không có lớp OSAL/HAL dùng chung. Phần tái sử dụng được chép sang rồi sửa trực tiếp |
| D3 | Web | Mongoose 7.x đảm nhận toàn bộ `webapi`: HTTP (UI, `/api/*`, upload) và WebSocket console. Chạy trên socket lwIP, **build không có TLS** |
| D4 | Phạm vi phần cứng | Đủ cả board: UART1 và UART2, mỗi cổng có socket riêng và TLS riêng; Edge trên cả hai bus; 2 AI. **UART1 không phân biệt RS232/RS485** |
| D5 | Build | CMake + arm-none-eabi-gcc. CubeMX chỉ dùng để sinh HAL/LL, clock, pinmux, startup vào thư mục `cube/` |
| D6 | Edge | **Tối đa 100 điểm** (bản ESP32: 300). `edgeDevices` xử lý **theo từng node** trong một arena cố định (§9) |
| D7 | Kiểm thử | Host unit test cộng bench HIL trên board thật. E2E chỉ kiểm **UART1** |
| D8 | MQTT | coreMQTT v2.3.1, chạy trong task riêng, đặt trên `tlsconn.c` chép từ ESP32. Outbox tự viết. Vá CONNECT để hỗ trợ MQTT 3.1 |
| D9 | Bootloader/OTA | Bootloader tự viết trong flash nội. Slot staging và backup nằm trên SPI flash. Cài đặt làm tiếp được sau khi mất điện, kiểm bằng SHA-256, có boot thử và rollback |
| D10 | LTE | PPPoS của lwIP qua **CMUX** trên UART3 (EC600U có hỗ trợ CMUX). Giữ đường PPP thẳng làm dự phòng |
| D11 | Giờ | NTP là tùy chọn, chủ yếu phục vụ report theo lịch. Không dịch vụ nào chờ đồng hồ (§10.4). Không thêm nguồn giờ nào khác |
| D12 | Loại bỏ | SNMP, Cloud, USB, **Telnet**, **RFC 2217**, **đồng bộ baud kiểu PUSR/USR-VCOM**, Socket B, máy in serial, IO mở rộng, PID |

**License:** Mongoose phát hành theo GPLv2 hoặc license thương mại của Cesanta. Firmware mã đóng thì phải mua license thương mại.

## 3. Phần cứng và sơ đồ chân

Lấy từ `docs/Job2.PDF`. Những mục có dấu (✓) phải đối chiếu lại với netlist và bảng alternate function trong CubeMX trước khi chốt file `.ioc`.

| Chức năng | Chân | Ghi chú |
|---|---|---|
| ETH RMII: REF_CLK / MDIO / CRS_DV | PA1 / PA2 / PA7 (✓) | IP101GRI, REF_CLK 50 MHz lấy từ PHY |
| ETH RMII: MDC / RXD0 / RXD1 | PC1 / PC4 / PC5 | |
| ETH RMII: TX_EN / TXD0 / TXD1 | PB11 / PB12 / PB13 | |
| ETH_RESET | PD13 | |
| USART1 TX / RX / **DE** | PA9 / PA10 / PA12 (AF7, DE phần cứng) | SN65HVD75 (RS485 #1) và SP3232 (RS232) **mắc song song**: TX đi ra cả hai, RX gộp qua diode MBR0540 |
| USART2 TX / RX / **DE** | PD5 / PD6 / PD4 (AF7) | SN65HVD75 (RS485 #2) |
| USART3 TX / RX | PD8 / PD9 | Tới MAIN_UART của EC600U qua bộ dịch mức UM3212 (1,8 V). **Không có RTS/CTS** |
| LTE_PWR / LTE_RESET | PA5 / PA4 (✓) | Qua NPN Q1/Q2: mức cao tương ứng nhấn PWRKEY / kích RESET_N |
| UART7 RX / TX | PB3 / PB4 | Log debug ra cổng P4 |
| UART8 RX / TX | PE0 / PE1 | Chưa dùng |
| QUADSPI CLK / NCS | PB2 / PC11 | **Bank 2** (PC11 = BK2_NCS) |
| QUADSPI IO0..IO3 | PE7..PE10 | GD25Q32ESIG 4 MB, XIP |
| SPI4 SCK / NSS / MISO / MOSI | PE2 / PE4 / PE5 / PE6 | GD25Q32ESIG 4 MB, dùng cho dữ liệu |
| DI1 / DI2 | PB7 / PB6 | Opto LTV-814 cộng pull-up: **mức 0 nghĩa là ngõ vào đang tác động** (không có cổng đảo như GW100) |
| DO1 / DO2 | PD10 / PD11 → 74HC573 D1/D2 | Q1/Q2 → S8050 → relay HF33F: mức cao làm relay hút |
| 74HC573 LE / OE | PD14 / PD15 | R108 và R103 kéo xuống GND. Lúc cấp nguồn latch có trạng thái bất định (§11.2) |
| AI1 / AI2 | PC2_C / PC3_C (✓ ADC/kênh) | 4–20 mA qua shunt 120 Ω, LM2904 hệ số 0,499, nên ADC thấy 0,24–1,2 V. VREF+ = REF3030 3,0 V |
| WDI (SGM706) + LED WORK | PC10 | Qua Q6/Q3 |
| LED_NET | PE11 | |
| Reload | PA8 | Tích cực mức thấp |
| SWD | PA13 / PA14 (P6) | |
| USB_HS D−/D+ | PB14 / PB15 | **Không dùng.** Dùng chung đường với J4 và EC600U, MCU để chân ở trạng thái hi-Z |
| HSE | PH0 / PH1 | Thạch anh Y1 25 MHz |
| LSE / VBAT | không có / nối 3V3 | Mất nguồn là mất RTC |

## 4. Cây mã nguồn và tái sử dụng

```
stm32/
├── docs/                 spec, kiến trúc
├── web/                  bản sao Web UI (Vite + Preact + Tailwind), phát triển độc lập
└── firmware/
    ├── CMakeLists.txt, CMakePresets.json (boot, app), cmake/arm-none-eabi.cmake
    ├── cube/             .ioc + code CubeMX sinh ra. Chỉ sửa trong các khối USER CODE
    ├── boot/             bootloader (flash nội), linker script riêng
    ├── app/              main.c, linker script XIP (0x90000000), FreeRTOSConfig.h, lwipopts.h, mbedtls_config.h, mongoose_config.h
    ├── components/       board cfgmodel iomgr netmgr proto serial services store syslog sysmgr webapi
    ├── third_party/      FreeRTOS-Kernel, lwIP, mbedTLS, Mongoose, coreMQTT 2.3.1, FlashDB, cJSON (khóa phiên bản); patches/
    ├── test/host/        unit test trên PC
    ├── test/bench/       test HIL
    └── tools/            embed_web.py, check_schema.py, audit_keys.py, mkimage.py
```

| Module | Cách xử lý | Thay đổi |
|---|---|---|
| `proto` | Chép nguyên | Không chép `rfc2217.c`. Thêm `jsonscan.c` (§9) và `cmux.c` (§7.2) |
| `cfgmodel` | Chép, sửa | Bật TLS cho `port2`, thêm chứng chỉ `scope=port2`. Xóa `telnet`, `telnetPort`, `rfc2217`. `MAX_NODES` = 100. API validate theo node (§9) |
| `services/bridge.c` | Chép, sửa | **Một instance cho mỗi cổng**: bỏ khoảng 22 chỗ viết cứng UART1. Bỏ nhánh RFC 2217 và PUSR |
| `services/edge_rt.c` | Chép, sửa | `edge_bus[n]` cho mỗi bus. Mô hình nạp vào struct C bằng đường xử lý theo node |
| `services/net.c`, `tlsconn.c` | Chép, sửa nhẹ | Gần như giữ nguyên: socket lwIP và mbedTLS dùng chung được cho cả hai nền tảng |
| `services/mqttgw.c` | Viết lại phần client | Logic topic, custom mode, distribute và IO control giữ nguyên. Client chuyển sang coreMQTT cộng outbox (§8) |
| `serial` | Giữ logic, viết lại driver | USART kèm DMA vòng, Receiver Timeout và DE phần cứng (§6.5) |
| `iomgr` | Giữ logic | `esp_timer` → software timer của FreeRTOS (callback chỉ gửi notify). Driver 74HC573, AI2, DI tích cực mức thấp |
| `store`, `syslog` | Chép, sửa | Port FAL mới cho SPI4. `edgeDevices` lưu theo node |
| `webapi` | Viết lại trên Mongoose | Giữ lại logic JSON của `api.c` và `console.c`. Không chép `telnet.c` |
| `netmgr` | Viết mới | ETH IP101, EC600U (CMUX, PPPoS), ưu tiên kết nối |
| `sysmgr`, `board` | Viết mới | SNTP của lwIP, OTA vào staging, watchdog, Reload, LED |

**Ánh xạ API của ESP-IDF sang STM32:**

| ESP-IDF | STM32 |
|---|---|
| `xRingbuffer*` | StreamBuffer/MessageBuffer của FreeRTOS |
| `esp_timer_get_time` | `now_us()`: TIM2 32-bit chạy 1 MHz, mở rộng lên 64 bit |
| `esp_log` | Macro của `syslog` |
| `esp_err_t` | `int`: 0 nghĩa là OK, mã lỗi âm |
| `heap_caps_*` | Cấp phát theo vùng (§5.3, §5.5) |

## 5. Bộ nhớ

### 5.1 Flash

| Vùng | Dung lượng | Nội dung |
|---|---|---|
| Flash nội (0x08000000) | 128 KB, **một sector duy nhất** | Chỉ chứa bootloader (≤ 64 KB). Không ghi gì vào đây lúc chạy |
| QSPI (0x90000000, XIP) | 4 MB | Ứng dụng tối đa 1.520 KB, gồm header 1 KB. UI gzip nằm trong `.rodata`. Không ghi được khi đang XIP |
| SPI4 | 4 MB | Bảng §5.2 |

### 5.2 SPI4

| Offset | Dung lượng | Nội dung |
|---|---|---|
| 0x000000 | 1.024 KB | FlashDB: `cfg` KVDB 256 KB (sector 16 KB), `edge` KVDB 256 KB, `cert` KVDB 128 KB, `log` TSDB 384 KB |
| 0x100000 | 32 KB | Trạng thái boot: hai bản ghi 4 KB ghi luân phiên (số thứ tự và CRC32) |
| 0x108000 | 1.520 KB | Slot staging |
| 0x284000 | 1.520 KB | Slot backup |

### 5.3 RAM

| Vùng | Dung lượng | Nội dung | Quy tắc |
|---|---|---|---|
| ITCM | 64 KB | Code nóng chép từ QSPI lúc khởi động: kernel FreeRTOS, các ISR (ETH, USART, DMA, TIM), `memcpy`/`memset`, lõi bignum của mbedTLS | Section `.itcm_text` |
| DTCM | 128 KB | MSP 4 KB, `.data`/`.bss` nhỏ, stack tĩnh của mọi task **trừ** `web` và `edge_tcpN` (khoảng 84 KB) | ETH và DMA1/2 không truy cập được vùng này. §5.4 bảo đảm không DMA nào đọc hay ghi trực tiếp ở đây |
| AXI SRAM (D1) | 512 KB | Heap FreeRTOS (`heap_5`, vùng duy nhất) 256 KB: 3 phiên TLS (port1, port2, MQTT), connection Mongoose, các cấp phát lúc chạy (§5.5). **Arena JSON 64 KB**. Mô hình Edge lúc chạy và cache giá trị khoảng 30 KB. Cache văn bản cấu hình của `store` khoảng 20 KB. Stack của `web` và `edge_tcpN` khoảng 33 KB | Cacheable (write-back). Dư khoảng 100 KB |
| SRAM1 (D2) | 128 KB | Descriptor và buffer RX của ETH, heap `MEM` của lwIP 64 KB, pool pbuf, ring DMA của USART1/2/3 | **Non-cacheable qua MPU.** Startup bật clock SRAM D2 trước khi khởi tạo `.data` |
| SRAM2+3 (D2) | 160 KB | Ring serial → mạng 2 × 8 KB, `bufferOffline` 2 × 32 KB, outbox MQTT 64 KB | Cacheable |
| SRAM4 (D3) | 64 KB | Ring syslog 32 KB, ring của `mqtt_io` 2 × 16 KB | |
| Backup SRAM | 4 KB | Bản ghi HardFault (§11.4) | |

Các buffer lớn đều cấp phát tĩnh trong section của vùng tương ứng. Heap chỉ dùng cho TLS, Mongoose và các cấp phát nhỏ lúc chạy (§5.5). Các con số trên là **ngân sách dự kiến**. Bộ test bench đo heap còn trống thấp nhất của từng vùng và high-water mark của từng stack (§12.3).

### 5.4 DMA và cache

Không có engine DMA nào đọc hay ghi trực tiếp vào buffer của bên gọi. Mỗi driver sở hữu buffer non-cacheable của riêng mình trong SRAM1 và tự chép dữ liệu vào hoặc ra:

| Engine | Quy tắc |
|---|---|
| ETH TX | `ethernetif` chép chuỗi pbuf vào buffer TX trong vùng non-cacheable (không zero-copy, `LWIP_NETIF_TX_SINGLE_PBUF=1`). Lý do: pbuf `PBUF_REF` của UDP `sendto` có thể trỏ vào stack trong DTCM hoặc vào AXI |
| ETH RX | Zero-copy từ pool pbuf RX đặt trong SRAM1 non-cacheable |
| USART1/2/3 TX | `serial_write` chép vào buffer TX non-cacheable của cổng (2 KB, chia theo từng khối) rồi mới khởi động DMA. Bên gọi (bridge, mqtt_io, console, lte) có thể dùng buffer ở bất kỳ đâu |
| USART RX | Ring DMA non-cacheable, `serial_rx[n]` chép frame ra ngoài |
| SPI4 (FAL) | Buffer trung chuyển non-cacheable 4 KB cho cả hai chiều. Buffer của FlashDB không bao giờ đi thẳng tới DMA |
| QSPI | Chỉ bootloader ghi, ở chế độ indirect kèm polling, không dùng DMA |

Nhờ quy tắc này, stack và buffer trong DTCM hay AXI đều an toàn với DMA.

### 5.5 Cấp phát bộ nhớ

- **newlib:** `malloc`/`calloc`/`realloc`/`free` được định tuyến sang `pvPortMalloc`/`vPortFree` (`heap_5`, thread-safe). `_sbrk` luôn trả lỗi, để không có cấp phát nào lọt ra ngoài vùng đã định. mbedTLS dùng `MBEDTLS_PLATFORM_MEMORY` trỏ về cùng heap, Mongoose cũng vậy. coreMQTT không tự cấp phát.
- **cJSON:** hook toàn cục `json_malloc`/`json_free` định tuyến **theo chủ sở hữu**.
  - Task đang giữ arena (`arena_begin()` đặt con trỏ thread-local) thì cấp phát từ arena. Task khác cấp phát từ heap.
  - `json_free` không làm gì với con trỏ nằm trong arena, vì arena được reset nguyên khối.

| Nơi gọi cJSON | Chạy trong task | Bộ cấp phát |
|---|---|---|
| `cfgmodel` (validate khi lưu và khi boot) | `web`, boot | Arena |
| `webapi` (`api.c`, `server.c`: parse request, dựng response) | `web` | Arena, mỗi request một phạm vi |
| `edgecore` (biên dịch template), nạp mô hình Edge | boot (`edge_rt`) | Arena |
| `store_get` cho các dịch vụ đọc cấu hình lúc khởi động | boot | Arena, mỗi dịch vụ một phạm vi |
| `rwprot` (truy vấn JSON tối đa 5 điểm), IO control | `mqtt`, `bridge[n]` | Heap (nhỏ, giải phóng ngay) |
| `iomgr` (JSON của IO) | `web`, `io` | Theo task gọi |

- **`store` lưu cache bằng văn bản, không bằng cây.** Bản ESP32 giữ lâu dài một cây cJSON cho mỗi cấu hình (`s_cache`), cách này không đứng được nếu không có PSRAM.
  - Bản STM32 giữ văn bản JSON compact của từng cấu hình nhỏ, tổng khoảng 20 KB trong AXI.
  - `store_get_json` trả thẳng văn bản này.
  - `store_get` parse văn bản ra cây trong bộ cấp phát của task gọi.
  - `edgeDevices` không được cache: khi cần thì đọc lại từ các bản ghi trong flash.

### 5.6 lwIP

Khoảng 64 netconn, đủ cho:
- 2 × 16 client của TCP server, kèm các listener;
- Mongoose: 2 listener, khoảng 8 connection web, 4 client WebSocket;
- MQTT, tối đa 5 thiết bị Modbus TCP, DNS, SNTP, socket UDP của bridge.

Cấu hình `TCP_SND_BUF` = `TCP_WND` = 4×MSS, `MEM_SIZE` 64 KB dùng chung, `LWIP_IGMP` bật. Khi hết bộ nhớ, socket trả về EAGAIN; `bridge` vẫn dùng `select()`.

## 6. Runtime

### 6.1 Các task

Thang ưu tiên của FreeRTOS là 0–31 (`configMAX_PRIORITIES` = 32). `n` là chỉ số cổng (1, 2).

| Task | Số lượng | Stack | Prio | Vai trò |
|---|---|---|---|---|
| `wdog` | 1 | 1 KB | 31 (cao nhất) | Kick WDI và nháy LED WORK mỗi 200 ms. Nằm trên `tcpip` và `eth_rx`, để một trận broadcast trên LAN nhà máy không gây vòng reset |
| `tcpip` | 1 | 4 KB | 24 | lwIP, SNTP, nhận dữ liệu PPPoS |
| `eth_rx` | 1 | 2 KB | 22 | Semaphore từ ISR của ETH, đưa pbuf vào lwIP |
| `serial_rx[n]` | 2 | 3 KB | 12 | Chủ đọc duy nhất của cổng: cắt frame, giao cho transaction hoặc subscriber |
| `restart` | khi cần | 3 KB | 10 | Chạy các shutdown handler rồi reset |
| `bridge[n]` | 2 | 6 KB | 8 | Socket của cổng theo chế độ làm việc, TLS, Modbus gateway, polling, heartbeat, registration |
| `io` | 1 | 3 KB | 6 | DI (10 ms), AI (500 ms), DO/74HC573, 6 timer, `keepOutputs` |
| `edge_ctl` | 1 | 4 KB | 6 | Điểm IO và điểm ảo, linkage mỗi 10 ms. Không chờ I/O |
| `mqtt` | 1 | 6 KB | 5 | Chủ duy nhất của coreMQTT: connect, `MQTT_ProcessLoop`, outbox, gửi lại, `reconnectIfSilent` |
| `mqtt_io` | 1 | 4 KB | 5 | Hai chiều: subscribe → serial, serial → outbox |
| `web` | 1 | 8 KB | 5 | `mg_mgr` của `webPort` |
| `console` | 1 | 4 KB | 5 | `mg_mgr` riêng của `websocketPort` |
| `edge_bus[n]` | 2 | 6 KB | 5 | Poll thiết bị trên bus của cổng n |
| `edge_tcpN` | ≤ 5 | 5 KB | 5 | Mỗi thiết bị Modbus TCP một task |
| `lte` | 1 | 6 KB | 5 | EC600U: nguồn, AT, CMUX, PPP, tín hiệu, phục hồi |
| `edge_rpt` | 1 | 4 KB | 4 | Report theo chu kỳ, theo lịch, khi thay đổi |
| `serial_hb[n]` | 2 | 2 KB | 4 | Heartbeat serial khi bus im lặng |
| `reload` | 1 | 2 KB | 3 | Nút Reload |
| `syslog`, `idle`, `ota_confirm` | 1 mỗi loại | 3 / 2 / 2 KB | 2 | Log, `idleRestart`, xác nhận image |

Thứ tự ưu tiên giữ nguyên lý do của bản ESP32:
- Byte UART không bao giờ phải chờ việc mạng.
- Request từ mạng tới được bus trước lần poll Edge kế tiếp.
- IO và linkage chạy trên tầng polling.

### 6.2 Bootloader và app chia nhau việc khởi tạo

Code trên QSPI không được cấu hình lại chính đường đang dùng để thực thi nó. Vì vậy:
- **Bootloader sở hữu:**
  - toàn bộ cây clock: HSE 25 MHz, PLL1 làm SYSCLK, PLL2 và PLL3 cấp kernel clock cho USART, SPI, ADC. Mục tiêu 480 MHz với chip rev V;
  - QUADSPI: bank 2 (`FSEL`=1), chế độ memory-mapped;
  - region MPU của QSPI.
- **App:**
  - **không** gọi `SystemClock_Config` và `MX_QUADSPI_Init`. Trong `.ioc` của app, hai hàm này đặt "Do not generate function call", vì HAL sẽ từ chối đổi PLL1 khi nó đang là SYSCLK, và khởi tạo lại QUADSPI sẽ làm hỏng XIP;
  - gọi `SystemCoreClockUpdate()` để đọc tần số từ thanh ghi RCC;
  - đặt VTOR = 0x90000400 trong startup;
  - lập lại toàn bộ MPU: region QSPI giống hệt bootloader, SRAM1 non-cacheable;
  - chép code vào ITCM;
  - bật I-cache và D-cache.

### 6.3 Thứ tự khởi động của app

1. `main()`, chưa chạy scheduler:
   - `board_early()`: MPU, cache, chép ITCM, đưa GPIO về trạng thái an toàn, kick WDI;
   - tạo task `wdog` và task `init`;
   - `vTaskStartScheduler()`.
2. Từ lúc này `wdog` kick độc lập. **Driver SPI4 nhường CPU (`vTaskDelay(1)`) trong lúc chờ flash bận**, nên ngay cả lần format FlashDB đầu tiên (xóa khoảng 1 MB, mất vài giây) cũng không làm lỡ nhịp WDI.
3. Task `init` lần lượt chạy:
   1. `store`;
   2. `syslog`;
   3. `netmgr`;
   4. `iomgr`, khôi phục `keepOutputs`;
   5. `serial`, phải chạy trước mọi module subscribe cổng nối tiếp;
   6. `services`: bridge[n], MQTT và Edge khi được bật;
   7. `webapi`;
   8. `sysmgr`, **chạy cuối cùng**. Module này nhận kết quả khởi động của các dịch vụ và khởi động `ota_confirm` (§11.3).
4. Task `init` tự xóa sau khi xong.

### 6.4 Quy tắc luồng dữ liệu

Giữ từ bản ESP32 và áp dụng **cho từng cổng**:
1. Mỗi UART chỉ có một task đọc là `serial_rx[n]`.
2. Mọi lệnh ghi, transaction và thay đổi tham số đường truyền đều phải qua **mutex bus** của cổng đó.
3. Subscriber chỉ đưa frame vào hàng đợi. Task serial không bao giờ tự gửi ra mạng.
4. Có reply window và kiểm slave như mô tả trong ARCHITECTURE §8.

Quy tắc riêng của bản STM32:
- **Mongoose:** chỉ `web` và `console` được gọi `mg_*`.
  - Dòng log và frame UART đi tới WebSocket qua một StreamBuffer. Lệnh "đóng mọi client khi đổi mật khẩu" (F19) đi qua một cờ.
  - `console` rút dữ liệu sau mỗi lần `mg_mgr_poll`, với timeout poll 20 ms.
- **coreMQTT:** chỉ task `mqtt` được gọi `MQTT_*`. Task khác đưa dữ liệu vào outbox (có mutex) rồi notify `mqtt`.
- **Nhiều cổng:**
  - Topic bind cổng qua bitmask `ports`.
  - `websocketTarget` nhận giá trị 0 (tắt), 1, 2 (UART) hoặc 3 (LOG).
  - Thiết bị Edge chọn bus qua trường `uart`.
  - `edgeReport.channel` nhận giá trị 0 (MQTT), 1 hoặc 2 (socket của cổng).
  - Modbus gateway của cổng n chỉ chuyển tiếp ra bus n. IO slave (`io.modbusAddress`) trả lời trên cả hai cổng.

### 6.5 Driver serial

- **Nhận:** DMA vòng (NDTR) cộng Receiver Timeout (`RTOR`) đặt theo `packetInterval`, hoặc 3,5 ký tự khi `packetInterval` = 0. Phần cứng báo khoảng lặng ký tự chính xác, nên **giới hạn t1.5 ghi ở mục F14 của bản ESP32 không còn**.
- Lỗi parity hoặc lỗi frame làm hỏng frame: frame bị bỏ và được đếm, như bản ESP32.
- **Gửi:** DMA, DE phần cứng (`DEAT`/`DEDT`). Mỗi lần phát đều bật DE. UART1 mắc song song RS232 nên điều này vô hại.
- Parity hỗ trợ None/Odd/Even. Mark/Space để lại như bản ESP32: bỏ khỏi UI. XON/XOFF xử lý bằng phần mềm trong driver.
- Baud 600–230400.

## 7. Mạng

### 7.1 Ethernet

- Driver ETH của HAL H7 dùng descriptor đặt trong SRAM1 non-cacheable. Phát hiện link qua MDIO, poll mỗi 1 s.
- IP tĩnh hoặc DHCP, DNS, hostname: lấy từ cấu hình `network`.
- Bật IGMP để `bridge` dùng multicast.

### 7.2 LTE EC600U

- **Khởi động:**
  1. Nếu `AT` không phản hồi trong 3 s thì nhấn PWRKEY 700 ms.
  2. Nếu vẫn không phản hồi thì kích `RESET_N`. Board này có chân reset, bản ESP32 thì không.
  3. Đồng bộ AT, đọc IMEI và ICCID.
  4. Gửi **`AT+CMUX=0,0,5,2048,20,3,30,10,2`** (lệnh nhóm vận hành đang dùng với EC600U) để chuyển sang CMUX (3GPP 27.010). Ý nghĩa tham số theo 27.007 §5.7:

     | Tham số | Giá trị | Ý nghĩa |
     |---|---|---|
     | mode | 0 | Basic mode |
     | subset | 0 | Chỉ dùng frame UIH |
     | port_speed | 5 | 115200 baud, khớp với UART3 |
     | N1 | 2048 | Kích thước trường thông tin tối đa của một frame |
     | T1 | 20 | Timer chờ ACK 200 ms |
     | N2 | 3 | Gửi lại tối đa 3 lần |
     | T2 | 30 | Timer phản hồi của kênh điều khiển 300 ms |
     | T3 | 10 | Timer phản hồi đánh thức 10 s |
     | k | 2 | Cửa sổ, chỉ dùng ở advanced mode, basic mode bỏ qua |

     Hệ quả cho bộ demux:
     - Hỗ trợ **trường độ dài 2 byte**, cần khi trường thông tin dài hơn 127 byte.
     - Buffer frame mỗi DLC ≥ 2048 + 6 byte, tức 2 KB 64 B trong AXI. Một gói PPP (MRU 1500) nằm gọn trong một frame.
     - Mở DLC0, rồi DLC1 và DLC2 bằng SABM, chờ UA trong khoảng T1, gửi lại tối đa N2 lần.
     - Đóng mux bằng lệnh CLD.
- **Kênh CMUX:**
  - DLC1 cho PPP: dữ liệu đi vào `pppos_input_tcpip`.
  - DLC2 cho AT: `AT+CSQ` và `AT+QNWINFO` mỗi 20 s.
- **Dự phòng:** nếu `AT+CMUX` bị từ chối thì chạy PPP thẳng. Thông tin tín hiệu khi đó chỉ được đọc trước khi PPP lên, giống bản ESP32.
- **Đường truyền UART3:** 115200 baud, RX bằng DMA vòng cộng ngắt IDLE. Không có RTS/CTS.
- **Phục hồi:** PPP không có địa chỉ quá 2 phút thì gửi `AT+CFUN=1,1`. Không phản hồi AT thì kích `RESET_N`.
- **PPP:** xác thực None/PAP/CHAP/tự thương lượng, APN và DNS lấy từ `network`.
- **Ưu tiên kết nối (`network.priority`)** giữ nguyên như ESP32:
  - 0: chỉ Ethernet;
  - 1: ưu tiên Ethernet;
  - 2: ưu tiên LTE.

  Default route chuyển sang link kia khi link đang ưu tiên mất địa chỉ.
- Các chuỗi hiển thị trên UI đổi từ EC800 sang EC600U.

### 7.3 TLS

- `tlsconn.c` chép từ ESP32, dùng chung cho `bridge[1]`, `bridge[2]` (TCP client và HTTPS) và MQTT.
- Giữ nguyên:
  - TLS 1.2;
  - `tlsVerify` 0/1/2;
  - SNI;
  - deadline cho handshake và cho thao tác ghi;
  - kiểm chứng chỉ khi upload (F20).
- `mbedtls_config.h`:
  - `MBEDTLS_SSL_IN_CONTENT_LEN` 16384, `OUT` 4096;
  - `MBEDTLS_HAVE_TIME_DATE`;
  - entropy lấy từ RNG phần cứng.
- **Khi chưa có giờ** (§10.4): mọi kết nối TLS, kể cả MQTT, chỉ bỏ qua kiểm tra ngày hiệu lực và ghi cảnh báo. Chuỗi chứng chỉ, CA và tên host vẫn kiểm đủ.

## 8. MQTT

- **Thư viện:** coreMQTT v2.3.1, transport đặt trên `tlsconn` (khi có TLS) hoặc socket thường.
- **Vá MQTT 3.1:** CONNECT dùng protocol name `MQIsdp`, level 3. Bản vá nằm ở `third_party/patches/coremqtt-mqtt31.patch` và có host test kiểm từng byte.
- **Outbox** (`services/mqtt_outbox.c`, đặt trong SRAM2+3):
  - Gửi lại QoS 1/2 theo packet id.
  - Giữ tối đa 1 giờ và 64 KB. Vượt mức thì bỏ tin mới và ghi cảnh báo.
  - Với `bufferOffline`, tin vẫn được xếp hàng khi broker không kết nối, kể cả QoS 0.
  - Không bao giờ chặn task serial.
- **Giữ hành vi của bản ESP32:**
  - phiên bản 3.1 và 3.1.1, clean session, keep-alive, xác thực, LWT;
  - `retryInterval`, `reconnectIfSilent`;
  - các chế độ publish 0/1 và custom mode, các chế độ subscribe 0/1, wildcard `+`/`#`;
  - IO control dạng JSON và Modbus RTU;
  - trạng thái `NO CA CERTIFICATE` và `NO CLIENT CERTIFICATE`.
  - Trạng thái `WAITING FOR CLOCK` **bị bỏ** (§10.4).

## 9. Lưu trữ và Edge xử lý theo node

### 9.1 FlashDB

- Port FAL cho SPI4: SPI kèm DMA, có lock riêng. `store` khóa mọi lời gọi FlashDB bằng một mutex đệ quy, như bản ESP32.
- Giữ các quy tắc của bản ESP32:
  - chỉ ghi khi lưu cấu hình;
  - trạng thái DO chỉ ghi khi bật `keepOutputs` và có thay đổi;
  - factory reset xóa `cfg`, `edge`, `cert` và giữ lại `log`;
  - validate lại toàn bộ cấu hình khi khởi động;
  - log TSDB đánh khóa bằng số thứ tự tăng dần.
- **Cần đo:** thời gian mở DB khi đọc qua SPI. Ước tính khoảng 5.300 lần đọc nhỏ, mỗi lần khoảng 5 µs, tổng chừng 30 ms.

### 9.2 Vì sao phải xử lý theo node

Đo trên host bằng `cfgmodel` hiện có, mô hình 300 điểm (khoảng 63 KB JSON):
- cây cJSON sau parse chiếm khoảng 360 KB;
- đỉnh lúc `cfg_validate` khoảng 700 KB;
- đỉnh lúc in ra khoảng 840 KB.

Với 100 điểm, đỉnh vẫn khoảng 235 KB. H750 không có PSRAM, nên không thể dựng cây cho toàn bộ mô hình.

### 9.3 Arena JSON

- Một vùng tĩnh 64 KB trong AXI, cấp phát tuyến tính và reset sau mỗi lần dùng, có một mutex.
- Chia thành: buffer body hoặc văn bản 32 KB, pool cho các cây cJSON nhỏ 24 KB, bảng tóm tắt 4 KB, dự phòng 4 KB.
- Chỉ ba nơi được giữ arena: `web` khi lưu cấu hình, `store` và `edge_rt` khi khởi động.
- Các cấu hình nhỏ (port, mqtt*, system, io, edgeReport, edgeLinkage) vẫn dùng cây cJSON nguyên khối. Với các cấu hình này, arena chia thành body ≤ 6 KB và 58 KB còn lại cho các cây: input, output và defaults. Theo tỷ lệ đã đo, cây chiếm khoảng 5,7 lần kích thước văn bản. Ước tính `mqttPublish` 16 topic đầy đủ (khoảng 4 KB) có đỉnh khoảng 46 KB.
- **Giới hạn được kiểm bằng test, không chỉ bằng ước tính:** host test validate payload kích thước tối đa của **từng** cấu hình với một bộ cấp phát có đếm, và kiểm đỉnh không vượt arena (§12.1).

### 9.4 Lưu `edgeDevices`

1. Ở `MG_EV_HTTP_HDRS`: `Content-Length` > 32 KB thì trả 413 kèm `{ "error": "…" }`. Nếu hợp lệ, handler nhận quyền kiểm soát kết nối và chép body vào arena.
2. `proto/jsonscan.c` duyệt văn bản và tìm ranh giới của từng object thiết bị và từng node trong `nodes`. Bộ duyệt xử lý đúng chuỗi, escape và ngoặc lồng nhau; thứ tự key trong object không quan trọng.
3. Với mỗi thiết bị:
   - parse **phần header** bằng `cJSON_ParseWithLength`, bỏ qua mảng `nodes`, rồi `cfg_edge_device()` validate theo `ITEM_SLAVE`;
   - với mỗi node, gọi `cfg_edge_node()` validate theo `ITEM_NODE` và các quy tắc F15.
4. `cfg_edge_begin/device/node/end` tích lũy bảng tóm tắt:
   - hash tên thiết bị và tên điểm;
   - tổng số điểm ≤ 100;
   - slave ID đã map;
   - khoảng thanh ghi đã map theo từng function code;
   - mỗi loại thiết bị IO và VIRTUAL chỉ có một.

   `cfg_edge_end()` chạy các kiểm tra liên thiết bị.
5. Mỗi phần hợp lệ được in compact và nối vào bản ghi 8 KB hiện hành `ed.<gen+1>.<i>`. Nếu tất cả hợp lệ thì ghi `edgeDevices = @<gen+1>,<count>`, tức đổi generation bằng một lần ghi. Gặp lỗi đầu tiên thì trả 4xx với câu thông báo như hiện tại, và generation mới bị bỏ.
6. **Định dạng lưu giữ nguyên như ESP32:** văn bản JSON compact chuẩn hóa, cắt thành các bản ghi 8 KB.

**Đọc (`GET`):** nối các bản ghi theo thứ tự, gửi bằng `mg_http_write_chunk`. Chỉ gửi chunk kế tiếp khi `c->send.len` < 4 KB.

**Khởi động:** đọc văn bản (≤ 32 KB) vào arena, gọi `cfg_load` theo node, dựng mô hình lúc chạy bằng struct C (thiết bị, node, chỉ mục FNV-1a, template report đã biên dịch), rồi giải phóng arena.

**Ràng buộc kiểm thử:** với mọi payload trong bộ test, kết quả xử lý theo node phải **giống từng byte** với kết quả của `cfg_validate` nguyên khối (§12.1).

## 10. Web, API và UI

### 10.1 Mongoose

- `MG_ARCH` là FreeRTOS kèm socket lwIP, `MG_TLS` = none. Hai `mg_mgr` chạy trong hai task (§6.1).
- **Body:**
  - Với `/api/ota`, `/api/cert` và `edgeDevices`: xử lý ở `MG_EV_HTTP_HDRS`, tự đọc `c->recv` theo luồng.
  - Các endpoint khác: giới hạn body 6 KB (§9.3), vượt quá thì trả 413. `MG_MAX_RECV_SIZE` đặt ở mức tương ứng.
- **Giữ hành vi của bản ESP32:**
  - UI gzip nhúng trong firmware, phục vụ không cần xác thực, ETag là hash nội dung từng file (F23);
  - Basic auth cho `/api/*`, so sánh thời gian hằng định;
  - trả 401 **không kèm** `WWW-Authenticate`;
  - đổi login có hiệu lực ngay và đóng mọi client WebSocket;
  - HTTP body hoặc OTA bỏ cuộc sau hai lần chờ 15 s không có dữ liệu.
- **WebSocket console** tại `/ws?auth=`:
  - kiểm login **trước** handshake, sai thì trả 401;
  - tối đa 4 client;
  - chế độ LOG nhận lệnh `help`, `status`, `restart`;
  - chế độ UART1 hoặc UART2 chuyển dữ liệu hai chiều dưới dạng binary.

### 10.2 Thay đổi API so với bản ESP32

| Mục | Thay đổi |
|---|---|
| `GET /api/device` | `portmax` = 2. `io` = `{do: 2, di: 2, ai: 2, ao: 0}`. Model và phần cứng là `SBIOT-H750` |
| `/api/config/port2` | Mới, cùng schema với `port1`. TLS có trên cả hai cổng |
| `/api/cert?scope=` | Thêm `port2` |
| `port<n>` | Bỏ `rfc2217` |
| `system` | Bỏ `telnet`, `telnetPort` |
| `edgeDevices` | Tối đa 100 điểm. Body ≤ 32 KB |
| `GET /api/status` | Thêm trạng thái của port2. Thêm `mem`: heap còn trống thấp nhất theo vùng (`axi`, `d2`, `sram4`) và stack còn dư thấp nhất cùng tên task. Thêm `wdiMaxGapMs` |
| Trạng thái MQTT | Bỏ `WAITING FOR CLOCK` |

### 10.3 Web UI bản STM32 (`stm32/web`)

Chép từ `src/` ở gốc, rồi sửa trực tiếp:
- `MAX_NODES` = 100;
- bỏ các trường Telnet (`System.tsx`) và RFC 2217 (`Port.tsx`);
- hiện TLS cho mọi cổng và cho phép upload chứng chỉ `port2`;
- đổi chuỗi EC800 thành EC600U;
- bỏ cảnh báo `WAITING FOR CLOCK`.

`npm run build:firmware` nhúng UI vào `stm32/firmware/components/webapi/www`.

### 10.4 Giờ

- NTP là tùy chọn. Chưa có giờ thì:
  - report theo `interval`, `onChange` và ngưỡng AI vẫn chạy;
  - report theo `schedule` và 6 timer chưa kích hoạt;
  - các trường `sys_time` để trống;
  - log ghi dạng `[boot N +hh:mm:ss]`.
- Nguồn giờ giữ như ESP32: NTP hoặc `POST /api/time`. **Không thêm** RTC giữ giờ hay giờ mạng LTE.

## 11. Bootloader, OTA và an toàn

### 11.1 Định dạng image

Header 1 KB đặt ở đầu image:

| Trường | Giá trị |
|---|---|
| magic | `SBIO` |
| mã phần cứng | `SBIOT-H750` |
| phiên bản | `major.minor.patch`, 3 × uint16 |
| kích thước | ≤ 1.519 KB |
| SHA-256 | |
| CRC của header | |

Vector table bắt đầu tại 0x90000400. `tools/mkimage.py` tạo header sau khi build.

### 11.2 Bootloader (flash nội, đơn luồng)

1. **Kick WDI ngay lập tức.** Mọi vòng lặp đều kick, kể cả vòng poll thanh ghi trạng thái của flash khi xóa hay ghi, nên thời gian xóa không ảnh hưởng watchdog.
2. **Ngõ ra an toàn:**
   - PD10/PD11 = 0, tạo xung LE, rồi OE = 0, để relay nhả;
   - LTE_PWR và LTE_RESET = 0;
   - giữ ETH_RESET ở mức reset.
3. Clock PLL, khởi tạo QSPI (bank 2) và SPI4. Đọc bản ghi trạng thái boot hợp lệ mới nhất.
4. **Cài đặt** khi trạng thái là `INSTALL_PENDING` hoặc `INSTALLING(pha, offset)`:
   - Các pha: `BACKUP` (QSPI → backup), `WRITE` (staging → QSPI), `VERIFY` (SHA-256 của QSPI so với header).
   - Đơn vị 64 KB gồm xóa, ghi và kiểm. Tiến độ ghi vào bản ghi trạng thái sau mỗi đơn vị.
   - Mất điện thì làm lại đơn vị đang dở. Nguồn chép vẫn còn nguyên.
   - Kết thúc ở trạng thái `TRIAL(count = 0)`.
5. **Boot thử:** `TRIAL` tăng `count` ở mỗi lần boot. `count` > 3 mà chưa được xác nhận thì khôi phục từ backup bằng cơ chế ở bước 4, rồi đặt `CONFIRMED`.
6. **Kiểm image ở mọi lần boot:** tính SHA-256 trên QSPI (ước tính 20–40 ms).
   - Image hỏng thì khôi phục từ backup.
   - Cả hai đều hỏng thì vào chế độ cứu hộ: LED WORK nháy nhanh, chỉ nạp lại được qua SWD.
7. Bật memory-mapped cho QSPI (MPU: cacheable, cho thực thi), đặt VTOR và MSP, rồi nhảy vào ứng dụng.

Logic trạng thái và cài đặt viết trong `boot/install.c` bằng C thuần, với giao diện flash trừu tượng. Mục đích là chạy được host test có cắt điện (§12.1).

**Đề xuất cho phần cứng (không bắt buộc):** đổi R103 thành kéo lên 3V3. Khi đó OE ở mức cao lúc cấp nguồn, relay không chập chờn trong khoảng vài ms trước khi bootloader chạy.

### 11.3 OTA phía ứng dụng

`POST /api/ota` chạy trong task `web`:
- Nhận theo luồng vào staging. Sector 4 KB được xóa ngay trước khi ghi tới.
- Kiểm header ngay từ khối đầu tiên: magic, mã phần cứng, kích thước. Trong lúc nhận, tính SHA-256 dần.
- Kết thúc mà khớp thì ghi `INSTALL_PENDING`, trả 200 rồi khởi động lại.
- Body dở hoặc sai SHA thì trả 400, trạng thái boot giữ nguyên.
- **Đang ở trạng thái `TRIAL` thì từ chối** với 409 `{ "error": "The running firmware is not confirmed yet; try again in a minute." }`. Nếu không chặn, pha BACKUP sẽ chép đè một image chưa được xác nhận lên bản backup vẫn đang chạy tốt.

`ota_confirm` đặt `CONFIRMED` khi mọi dịch vụ đã cấu hình đều khởi động được và đã chạy 30 s (F12). Dịch vụ nào khởi động lỗi thì không xác nhận, và bootloader sẽ rollback.

### 11.4 Các cơ chế an toàn khác

- **SGM706:** `wdog` kick mỗi 200 ms, so với timeout danh định 1,6 s. Giá trị tối thiểu cần xác minh theo datasheet (§14). Vì `wdog` là task chứ không phải ISR, scheduler treo thì watchdog sẽ reset board.
- **HardFault và vi phạm MPU:** handler ghi PC, LR, CFSR, HFSR, MMFAR/BFAR và tên task vào backup SRAM, rồi reset. Lần boot sau, thông tin này được ghi vào log. Đây là phần thay cho phân vùng coredump của bản ESP32.
- **Reload:** nhấn 3–15 s để khôi phục cấu hình gốc, giữ lại log.
- **BOR:** đặt option byte BOR level 2.

## 12. Kiểm thử và công cụ

### 12.1 Host test (`test/host`, ASan/UBSan, `-Werror`, cJSON lấy từ `third_party`)

- Chép từ ESP32: test của `cfgmodel` và `proto`, bỏ phần RFC 2217 và Telnet.
- `jsonscan`: chuỗi chứa ngoặc hoặc dấu phẩy, escape, thứ tự key bất kỳ, văn bản cụt.
- **Tương đương Edge:** chạy các payload trong mock của UI và trong bộ test cũ (thu về ≤ 100 điểm) qua đường xử lý theo node. Văn bản chuẩn hóa và câu báo lỗi phải giống từng byte với `cfg_validate` nguyên khối.
- **Cài đặt của bootloader:** flash giả lập, cắt điện ở **mọi** bước ghi của BACKUP, WRITE và VERIFY, cùng các trường hợp boot thử và rollback. Trạng thái cuối luôn boot được.
- **Đỉnh arena:** payload kích thước tối đa của từng cấu hình (đường cây nguyên khối), và mô hình Edge 100 điểm với tên và formula dài nhất (đường theo node). Bộ cấp phát có đếm, giả lập kích thước node cJSON trên 32-bit. Đỉnh phải ≤ 64 KB.
- Outbox MQTT: gửi lại, hết hạn, giới hạn kích thước, `bufferOffline`.
- `cmux`: mã hóa và giải mã frame, FCS, frame bị cắt, frame có trường độ dài 1 byte và 2 byte (kể cả trường thông tin đúng 2048 byte), mở DLC bằng SABM/UA kèm hết thời gian T1 và gửi lại N2 lần, đóng bằng CLD.
- Byte của gói CONNECT cho MQTT 3.1 và 3.1.1.

### 12.2 Bench HIL (`test/bench`)

- **Hạ tầng:**
  - Board và máy test cùng LAN.
  - **UART1** nối qua adapter USB-RS485, được bọc thành đúng giao tiếp socket mà các test hiện dùng.
  - Một relay USB cắt và cấp lại nguồn. `bench_env.py` thay cho `qemu_env.py`.
- **Các bộ test port từ `test/qemu`:** bridge, MQTT, Edge, misc, OTA.
  - Bỏ các test của Telnet, RFC 2217 và PUSR.
  - Mô hình Edge ≤ 100 điểm.
- **Bổ sung các mục chỉ kiểm được trên phần cứng:**
  - timing DE của RS485, XON/XOFF;
  - DI, DO qua 74HC573, AI1 và AI2 (hiệu chuẩn);
  - PPP + CMUX trên EC600U, chuyển đổi ưu tiên kết nối;
  - multicast;
  - `wdiMaxGapMs` trong lúc OTA và lúc FlashDB dọn rác;
  - **OTA bị cắt điện ngẫu nhiên 20 lần**.

### 12.3 Kiểm bộ nhớ

Bộ test đọc `/api/status.mem` sau mỗi suite ở tải tối đa:
- 2 × 16 client TCP;
- 3 phiên TLS;
- 4 client WebSocket;
- mô hình Edge 100 điểm.

Điều kiện đạt: mọi stack còn dư ≥ 1 KB, heap của mỗi vùng còn dư ≥ 16 KB.

### 12.4 Công cụ, build và nạp

- `embed_web.py`, `check_schema.py`, `audit_keys.py`: bản riêng, trỏ vào `stm32/web` và `stm32/firmware`.
- `mkimage.py`: thêm header.
- CMake preset `boot` và `app`. Kết quả gồm `boot.elf`, `app.elf` và `app.img` (đã có header).
- Nạp lần đầu qua SWD (P6) bằng OpenOCD với flash bank `stmqspi` (QSPI bank 2), cộng flash nội. Đường thay thế là STM32CubeProgrammer, nhưng cần một external loader viết riêng cho board.
- Log debug ra UART7, 115200 baud.

## 13. Khác biệt so với bản ESP32

| Mục | ESP32 (GW100) | STM32 (H750) |
|---|---|---|
| Cổng nối tiếp | 1 × RS485 | UART1 (RS485 và RS232 song song) và UART2 (RS485) |
| AI | 1, khoảng 12 bit hiệu dụng | 2, ADC 16 bit, VREF 3,0 V |
| TLS bridge | Chỉ UART1 | Cả hai cổng |
| Edge | 300 điểm, body 96 KB | **100 điểm, body 32 KB** |
| `bufferOffline` | 64 KB | **32 KB mỗi cổng** |
| Telnet, RFC 2217, PUSR VCOM | Có | **Bỏ** |
| MQTT TLS khi chưa có giờ | Chờ (`WAITING FOR CLOCK`) | **Bỏ qua kiểm tra ngày**, giống bridge |
| Modem | EC800, không có RESET_N | EC600U, có RESET_N, CMUX |
| Cắt frame RTU | Theo khoảng lặng, không đo được t1.5 | Receiver Timeout phần cứng |
| OTA | Hai slot ESP-IDF kèm rollback | Bootloader riêng: staging, backup, boot thử |
| Coredump | Phân vùng flash | Bản ghi HardFault trong backup SRAM |
| Web | `esp_http_server` | Mongoose |
| MQTT | esp-mqtt | coreMQTT cộng outbox riêng |

## 14. Rủi ro và các mục cần xác minh

| Rủi ro hoặc mục cần xác minh | Cách xử lý |
|---|---|
| Hiệu năng XIP (TLS handshake, Edge 10 ms) | Đưa code nóng vào ITCM. Đo thời gian handshake và độ trễ linkage trên bench |
| Cache và DMA (ETH, USART) | Buffer DMA chỉ đặt trong SRAM1 non-cacheable. Test bench có tải lớn |
| Ngân sách RAM | Đo `mem` (§12.3). Nếu thiếu, giảm `MEM_SIZE` hoặc số WebSocket client trước khi đụng tới tính năng |
| EC600U và CMUX theo phiên bản firmware của module | Giữ đường dự phòng PPP thẳng. Ghi phiên bản firmware của module vào log |
| UM3212 ở 115200 baud, UART3 không có RTS/CTS | Đo tỷ lệ lỗi ở 115200 và chỉ tăng baud khi đã đo |
| Chân và kênh ADC đánh dấu (✓) ở §3 | Đối chiếu netlist và CubeMX trước khi chốt `.ioc` |
| Timeout tối thiểu của SGM706, thời gian xóa tối đa của GD25Q32E | Lấy từ datasheet và ghi vào spec. Bootloader kick trong vòng poll nên không phụ thuộc vào thời gian xóa |
| Thời gian mở FlashDB qua SPI | Đo khi bring-up. Nếu chậm thì thêm cache header trong RAM |
| OpenOCD `stmqspi` trên bank 2 | Script cấu hình phải đặt `FSEL`=1 (và chân BK2) trước khi probe flash. Kiểm ngay ở bước bring-up đầu tiên |
| License Mongoose | Quyết định GPLv2 hay thương mại trước khi phát hành |

## 15. Ngoài phạm vi

- SNMP, Cloud (AWS, Azure, PUSR…), USB (cả MCU và đường USB tới EC600U), Telnet, RFC 2217, PUSR/USR-VCOM.
- Socket B, máy in serial, IO mở rộng, PID, UART8.
- Ký image và secure boot. F01 vẫn được chấp nhận như bản ESP32.
- Nguồn giờ ngoài NTP và đặt tay.
- Thay đổi firmware và UI của ESP32.
