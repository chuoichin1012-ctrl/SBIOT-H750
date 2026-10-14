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
4. Đo trên board thật ở hai kịch bản tải tối đa (§12.3): stack còn dư ≥ 1 KB ở mọi task, heap AXI còn ≥ 16 KB, đỉnh arena ≤ 60 KB, không pool tĩnh nào đầy ngoài thiết kế.
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
| AI1 / AI2 | PC2_C / PC3_C = **ADC3_INP0 / ADC3_INP1** (single-ended) | 4–20 mA qua shunt 120 Ω, LM2904 hệ số 0,499, nên ADC thấy 0,24–1,2 V. VREF+ = REF3030 3,0 V. ADC3 chạy **oversampling phần cứng, kích bằng timer, BDMA vòng vào SRAM4** (§6.6) |
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
| QSPI (0x90000000, XIP) | 4 MB | Ứng dụng tối đa 1.472 KB, gồm header 1 KB. UI gzip nằm trong `.rodata`. Không ghi được khi đang XIP |
| SPI4 | 4 MB | Bảng §5.2 |

### 5.2 SPI4

Mọi vùng đều **căn theo 64 KB**, nên một lệnh xóa block không bao giờ lấn sang vùng bên cạnh (GD25Q32E §7.17).

| Offset | Dung lượng | Nội dung |
|---|---|---|
| 0x000000 | 1.024 KB | FlashDB: `cfg` KVDB 256 KB (sector 16 KB), `edge` KVDB 256 KB, `cert` KVDB 128 KB, `log` TSDB 384 KB |
| 0x100000 | 64 KB | Trạng thái boot: hai bản ghi, mỗi bản một sector 4 KB riêng (§11.2). Phần còn lại để dự trữ |
| 0x110000 | 1.472 KB (0x170000, 23 × 64 KB) | Slot staging |
| 0x280000 | 1.472 KB | Slot backup |
| 0x3F0000 | 64 KB | Factory record (hiệu chuẩn AI, §6.6): hai sector 4 KB, mỗi sector một bản sao kèm CRC. Ghi bằng công cụ sản xuất. Factory reset không xóa. Phần còn lại để dự trữ |

Mọi thao tác trên slot dùng cùng một cách chia:
- **checkpoint** theo đơn vị logic 64 KB;
- **xóa** theo sector 4 KB;
- **ghi** theo page 256 B.

Với checkpoint cuối, chỉ **chép và tính hash** đúng số byte thuộc image. Phần **xóa** thì làm tròn lên thành số sector 4 KB nguyên (⌈byte còn lại / 4 KB⌉), vì không thể xóa dở một sector. Cách làm tròn này không bao giờ vượt quá cuối slot, vì slot dài đúng 23 × 64 KB.

### 5.3 RAM

| Vùng | Dung lượng | Nội dung | Quy tắc |
|---|---|---|---|
| ITCM | 64 KB | Code nóng chép từ QSPI lúc khởi động: kernel FreeRTOS, các ISR (ETH, USART, DMA, TIM), `memcpy`/`memset`, lõi bignum của mbedTLS | Section `.itcm_text` |
| DTCM | 128 KB | MSP 4 KB, `.data`/`.bss` nhỏ, stack tĩnh của mọi task **trừ** `web` và `edge_tcpN` (khoảng 95 KB theo §6.1, kể cả `init`, timer và `cmux_rx`), TCB. Tổng DTCM ≤ 120 KB, kiểm bằng file map của linker | ETH và DMA1/2 không truy cập được vùng này. §5.4 bảo đảm không DMA nào đọc hay ghi trực tiếp ở đây |
| AXI SRAM (D1) | 512 KB | Heap FreeRTOS (`heap_5`, **vùng heap duy nhất**) 256 KB: 3 phiên TLS, connection Mongoose, các cấp phát nhỏ lúc chạy (§5.5). **Arena JSON 64 KB** (§9.3). **Buffer văn bản Edge 48 KB** (§9.3). Mô hình Edge lúc chạy và cache giá trị khoảng 20 KB. **Buffer report ≤ 26 KB** (§9.4). Cache văn bản cấu hình của `store` khoảng 20 KB. Stack của `web` và `edge_tcpN` khoảng 33 KB. Buffer mạng của coreMQTT 2 × 4 KB, lấy từ heap | Cacheable (write-back). Dư khoảng 45 KB |
| SRAM1 (D2) | 128 KB | Descriptor, buffer RX và buffer TX của ETH, heap `MEM` của lwIP 64 KB, pool pbuf, ring DMA RX và buffer TX của USART1/2/3, buffer trung chuyển SPI4. Số lượng và kích thước chốt trong bảng pool lwIP (§5.6) | **Non-cacheable qua MPU.** Startup bật clock SRAM D2 trước khi khởi tạo `.data` |
| SRAM2+3 (D2) | 160 KB | Ring serial → mạng 2 × 8 KB, `bufferOffline` 2 × 32 KB, outbox MQTT 64 KB. Còn 16 KB cho metadata và căn lề | Cacheable |
| SRAM4 (D3) | 64 KB | Ring syslog 32 KB, ring của `mqtt_io` 2 × 12 KB, **buffer BDMA của ADC3** (800 B) trong region non-cacheable 1 KB riêng ở 0x3800FC00 (§6.6). Còn khoảng 7 KB | Region 1 KB của ADC đặt non-cacheable qua MPU, phần còn lại cacheable |
| Backup SRAM | 4 KB | Bản ghi HardFault (§11.4) | |

**Cách tính ngân sách:**
- Các buffer lớn đều cấp phát tĩnh trong section của vùng tương ứng. Ngân sách tĩnh được kiểm bằng **file map của linker**: tổng của mỗi section không vượt dung lượng vùng.
- Chỉ AXI có heap. Các vùng D2 và SRAM4 chỉ chứa pool và buffer tĩnh, nên với các vùng này cái cần đo là **mức chiếm dụng** của pool (ví dụ pbuf, outbox, ring), không phải heap.
- Các con số trên là ngân sách dự kiến, được đo lại trên bench (§12.3).

### 5.4 DMA và cache

Không có engine DMA nào đọc hay ghi trực tiếp vào buffer của bên gọi. Mỗi driver sở hữu buffer non-cacheable của riêng mình, ở SRAM1, riêng ADC ở SRAM4, và tự chép dữ liệu vào hoặc ra:

| Engine | Quy tắc |
|---|---|
| ETH TX | `ethernetif` (`low_level_output`) **tự chép** chuỗi pbuf vào một buffer TX trong vùng non-cacheable. Chỉ đặt `LWIP_NETIF_TX_SINGLE_PBUF` thì chưa đủ. Lý do phải chép: pbuf `PBUF_REF` của UDP `sendto` có thể trỏ vào stack trong DTCM hoặc vào AXI. Buffer TX chỉ được dùng lại khi DMA đã trả descriptor về (bit OWN) |
| ETH RX | Zero-copy từ pool pbuf RX đặt trong SRAM1. pbuf được đưa lại vào descriptor chỉ sau khi lwIP đã giải phóng nó (custom pbuf free) |
| USART1/2/3 TX | `serial_write` chép vào buffer TX non-cacheable của cổng (2 KB, chia theo từng khối) rồi mới khởi động DMA. Chỉ chép tiếp vào buffer khi DMA đã báo xong (TC). Bên gọi (bridge, mqtt_io, console, lte) có thể dùng buffer ở bất kỳ đâu |
| USART RX | Ring DMA non-cacheable, `serial_rx[n]` chép frame ra ngoài |
| SPI4 (FAL) | Buffer trung chuyển non-cacheable 4 KB cho cả hai chiều. Buffer của FlashDB không bao giờ đi thẳng tới DMA |
| QSPI | Chỉ bootloader ghi, ở chế độ indirect kèm polling, không dùng DMA |
| ADC3 | **BDMA** (DMAMUX2, request 17) ghi vòng vào buffer trong region non-cacheable 1 KB của SRAM4 (§6.6). ADC3 cũng có request trên DMAMUX1 (115, tới DMA1/2). Dùng BDMA là **lựa chọn thiết kế**, để ADC không chiếm stream của DMA1/2 và không chiếm SRAM1 |

**Quy tắc chung cho mọi buffer DMA:**
- Căn lề 32 byte.
- Có `__DMB()` trước khi trao descriptor hoặc buffer cho DMA, và sau khi lấy lại.
- **Engine dùng trong firmware:** ETH, DMA1, DMA2 và BDMA (chỉ cho ADC3). MDMA không dùng.
  - BDMA chỉ truy cập được SRAM4.
  - ETH không truy cập được SRAM4.
  - Không engine nào trong bốn engine trên truy cập được TCM. MDMA thì truy cập được, nhưng không dùng.

Nhờ quy tắc này, stack và buffer trong DTCM hay AXI đều an toàn với DMA.

### 5.5 Cấp phát bộ nhớ

- **newlib:**
  - `malloc`, `calloc`, `realloc` và `free` được định tuyến sang `heap_5`, qua một wrapper mỏng.
  - Wrapper đặt một header 8 byte ghi kích thước trước mỗi block, giữ căn lề 8 byte.
    - `realloc` dùng header này để cấp phát block mới, chép dữ liệu, rồi giải phóng block cũ. Nếu cấp phát thất bại thì block cũ được giữ nguyên.
    - `calloc` kiểm tràn số khi nhân.
  - Bật `configUSE_NEWLIB_REENTRANT` để newlib an toàn khi chạy đa task.
  - `_sbrk` luôn trả lỗi, nên không cấp phát nào lọt ra ngoài heap.
  - Heap và các hook được khởi tạo trong `board_early()` (§6.3), **trước mọi cấp phát**.
  - Kiểm bằng symbol map: chỉ có một bản `malloc` trong binary, và không có tham chiếu nào tới `_sbrk_r` thật.
- **mbedTLS** dùng `MBEDTLS_PLATFORM_MEMORY` trỏ về wrapper trên. **Mongoose** cũng vậy. **coreMQTT** không tự cấp phát.
- **cJSON:** hook toàn cục `json_malloc`/`json_free` định tuyến **theo chủ sở hữu**.
  - Task đang ở trong một phạm vi arena (`arena_begin()` đặt con trỏ thread-local) thì cấp phát từ arena. Mọi trường hợp khác cấp phát từ heap.
  - `json_free` trả block về đúng nơi đã cấp nó, nhận biết theo khoảng địa chỉ.

| Nơi gọi cJSON | Chạy trong task | Bộ cấp phát |
|---|---|---|
| `cfgmodel` (validate khi lưu và khi boot) | callback của `web`, `init` | Arena |
| `webapi` (`api.c`, `server.c`: parse request, dựng response) | callback của `web` | Arena, mỗi callback một phạm vi |
| Nạp mô hình Edge | `init` | Arena. **Dữ liệu sống lâu hơn phạm vi phải được chép ra heap** (§9.3) |
| `edgecore` (biên dịch template report) | `init` | **Heap**, vì template sống suốt thời gian chạy (§9.3) |
| `store_get` cho các dịch vụ đọc cấu hình lúc khởi động | `init` | Arena, mỗi dịch vụ một phạm vi |
| `rwprot` (truy vấn JSON tối đa 5 điểm), IO control | `mqtt`, `bridge[n]` | Heap (nhỏ, giải phóng ngay) |
| `iomgr` (JSON của IO) | `web`, `io` | Theo task gọi |

- **`store` lưu cache bằng văn bản, không bằng cây.** Bản ESP32 giữ lâu dài một cây cJSON cho mỗi cấu hình (`s_cache`), cách này không đứng được nếu không có PSRAM.
  - Bản STM32 giữ văn bản JSON compact của từng cấu hình nhỏ, tổng khoảng 20 KB trong AXI, có mutex của `store`.
  - `store_get_json` **giữ nguyên hợp đồng của bản ESP32**: trả về một bản sao cấp từ heap, bên gọi sở hữu và tự giải phóng. Lý do: `api.c` gọi `free()` trên kết quả.
  - `store_get` parse văn bản ra cây, dùng bộ cấp phát của task gọi.
  - `edgeDevices` không được cache: khi cần thì đọc lại từ các bản ghi trong flash (§9.4).

### 5.6 lwIP

Khoảng 64 netconn, đủ cho:
- 2 × 16 client của TCP server, kèm các listener;
- Mongoose: 2 listener, khoảng 8 connection web, 4 client WebSocket;
- MQTT, tối đa 5 thiết bị Modbus TCP, DNS, SNTP, socket UDP của bridge.

Cấu hình chính: `TCP_SND_BUF` = `TCP_WND` = 4×MSS, `MEM_SIZE` 64 KB dùng chung, bật `LWIP_IGMP`.

- **Bảng pool:** việc đầu tiên của module `netmgr` trong kế hoạch là lập bảng pool. Bảng chốt kích thước và vùng đặt của từng pool:
  - `MEMP_NUM_NETCONN`, `MEMP_NUM_TCP_PCB`, `MEMP_NUM_TCP_PCB_LISTEN`, `MEMP_NUM_UDP_PCB`;
  - `MEMP_NUM_TCP_SEG`, `PBUF_POOL_SIZE`, `MEMP_NUM_NETBUF`;
  - các mailbox của `tcpip` và netconn;
  - `LWIP_SOCKET_OFFSET` và số socket tối đa;
  - DNS và PPP.

  Các pool này nằm ngoài `MEM_SIZE`. Mức chiếm dụng của chúng được báo trong `/api/status.mem`.
- **Thiếu bộ nhớ là lỗi tạm thời, không phải lỗi kết nối.** lwIP ánh xạ `ERR_MEM` thành `ENOMEM` và `ERR_BUF` thành `ENOBUFS`. Các mã `EWOULDBLOCK`/`EAGAIN`, `ENOMEM` và `ENOBUFS` đều được xử lý như "thử lại sau": dữ liệu vẫn nằm trong ring của bridge và socket không bị đóng.
- **Kiểm soát nhận kết nối:** khi pool `netconn` còn dưới 4, listener của các TCP server ngừng `accept`.

## 6. Runtime

### 6.1 Các task

Thang ưu tiên của FreeRTOS là 0–31 (`configMAX_PRIORITIES` = 32). `n` là chỉ số cổng (1, 2).

| Task | Số lượng | Stack | Prio | Vai trò |
|---|---|---|---|---|
| `wdog` | 1 | 2 KB | 31 (cao nhất) | Kick WDI và nháy LED WORK mỗi 200 ms (SGM706: timeout tối thiểu 1,0 s). Giám sát `init` và `TRIAL` (§11.4). Nằm trên `tcpip` và `eth_rx`, để một trận broadcast trên LAN nhà máy không gây vòng reset |
| `tcpip` | 1 | 4 KB | 24 | lwIP, SNTP, nhận dữ liệu PPPoS |
| `eth_rx` | 1 | 2 KB | 22 | Semaphore từ ISR của ETH, đưa pbuf vào lwIP |
| `serial_rx[n]` | 2 | 3 KB | 26 | Chủ đọc duy nhất của cổng: cắt frame, giao cho transaction hoặc subscriber. Mỗi lần thức dậy chỉ làm một lượng việc có giới hạn rồi block chờ ISR |
| `cmux_rx` | 1 | 2 KB | 25 | Rút ring DMA của UART3, ráp frame CMUX, đưa dữ liệu DLC1 vào `pppos_input_tcpip`, đưa dữ liệu DLC2 cho `lte`. Có giới hạn việc mỗi lượt, block chờ ISR |
| `restart` | khi cần | 3 KB | 10 | Chạy các shutdown handler rồi reset |
| `bridge[n]` | 2 | 6 KB | 8 | Socket của cổng theo chế độ làm việc, TLS, Modbus gateway, polling, heartbeat, registration |
| `io` | 1 | 3 KB | 6 | DI (10 ms). AI: khi có notify HT/TC (mỗi 100 ms) thì cộng một nửa buffer BDMA, và chạy phục hồi khi ADC lỗi (§6.6). DO/74HC573, 6 timer, `keepOutputs` |
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

Ngoài bảng trên còn có:
- task `init` (6 KB, prio 7), tự xóa sau khi khởi động xong;
- task timer của FreeRTOS (2 KB, prio 30). Callback của nó chỉ được gửi notify.

**Điều ưu tiên bảo đảm:**
- **Không mất byte serial do tải mạng.** Các task rút ring DMA (`serial_rx[n]` 26 và `cmux_rx` 25) có ưu tiên **cao hơn** `tcpip` (24) và `eth_rx` (22). Chúng chỉ làm lượng việc có giới hạn (chép frame, chuyển tiếp) rồi block chờ ISR, nên không gây đói các task khác.
  - Khoảng lặng do Receiver Timeout và timer phát hiện trong ISR (§6.5).
  - Ring RX của USART1/2 là 2 KB, đủ cho khoảng 90 ms ở 230400 baud. Ring của UART3 là 4 KB, đủ cho khoảng 350 ms ở 115200 baud.
  - Tràn ring được đếm thành `overrun`.
  - **Tiêu chí trên bench:** 0 lần overrun trên UART1 (230400 baud, liên tục) và UART3 (PPP tải liên tục), trong lúc LAN bị bơm broadcast 90 Mbit/s.
- **ETH không làm đói các task thấp hơn.** Mỗi lần thức dậy, `eth_rx` xử lý tối đa 8 frame. Nếu vẫn còn descriptor chờ, nó `vTaskDelay(1)`. Đây là block thật, nên các task thấp hơn được chạy, kể cả khi LAN bị flood. Bench kiểm: Web UI vẫn trả lời và `wdiMaxGapMs` < 400 ms khi LAN bị flood.
- Request từ mạng tới được bus trước lần poll Edge kế tiếp, vì `bridge` (8) cao hơn `edge_bus` (5).
- IO và linkage (6) chạy trên tầng polling (5).

### 6.2 Bootloader và app chia nhau việc khởi tạo

Code trên QSPI không được cấu hình lại chính đường đang dùng để thực thi nó.

**Bootloader sở hữu:**
- Toàn bộ cây clock: HSE 25 MHz, PLL1 làm SYSCLK, PLL2 và PLL3 cấp kernel clock cho USART, SPI, ADC. Mục tiêu 480 MHz với chip rev V (VOS0).
- QUADSPI, gồm:
  - bank 2 (`FSEL`=1), chân BK2;
  - bật bit **QE** của GD25Q32E nếu chưa bật (ghi vào thanh ghi trạng thái non-volatile, chỉ ghi khi bit đang tắt);
  - lệnh đọc Quad I/O Fast Read (`0xEB`) với số dummy cycle theo datasheet ở tần số đã chọn;
  - prescaler sao cho clock QSPI ≤ giới hạn của GD25Q32E ở 3,3 V;
  - trước khi cấu hình, gửi Reset Enable và Reset (`0x66`, `0x99`) để đưa flash về trạng thái đã biết, vì MCU có thể vừa reset trong lúc flash đang ở một chế độ khác;
  - bật memory-mapped.
- Region MPU của QSPI.

**Trạng thái bàn giao cho app** (bootloader bảo đảm, app kiểm bằng `assert` khi build debug): PLL1 đang chạy và là SYSCLK; QSPI ở memory-mapped; ngắt bị tắt (`PRIMASK`=1); SysTick tắt; mọi ngắt NVIC bị tắt và không còn ngắt chờ xử lý.

**App:**
- **Startup và `SystemInit` riêng cho XIP.** Startup GCC chuẩn của ST gọi `SystemInit()` *trước* `main()`, và hàm này reset RCC, tắt PLL. Vì vậy app dùng `system_app.c` riêng, không đụng tới RCC, QSPI và pinmux.
- Trình tự trong `Reset_Handler` của app:
  1. bật clock SRAM D2/D3;
  2. đặt VTOR = 0x90000400;
  3. chép `.data`, xóa `.bss`, chép `.itcm_text` và `.dtcm_data`;
  4. lập MPU (region QSPI giống hệt bootloader, SRAM1 non-cacheable, region ADC 1 KB ở 0x3800FC00 non-cacheable và XN, chặn truy cập địa chỉ 0). Đây là **lần duy nhất** cấu hình MPU, và nó xong trước khi bất kỳ DMA nào được bật;
  5. bật I-cache và D-cache;
  6. `SystemCoreClockUpdate()`;
  7. `main()`.
- Trong `.ioc` của app, đặt "Do not generate function call" cho `SystemClock_Config` và `MX_QUADSPI_Init`. HAL sẽ từ chối đổi PLL1 khi nó đang là SYSCLK, và khởi tạo lại QUADSPI sẽ làm hỏng XIP.
- **Kiểm chứng:** disassembly của `Reset_Handler` không gọi các hàm RCC/QSPI của HAL. Boot nguội (cấp nguồn từ đầu) và boot ấm (reset bằng watchdog) đều chạy được.

### 6.3 Thứ tự khởi động của app

1. `main()`, chưa chạy scheduler:
   - `board_early()`: khởi tạo heap và các hook cấp phát, đưa GPIO về trạng thái an toàn, kick WDI. MPU, cache và ITCM **đã** được `Reset_Handler` làm (§6.2), không làm lại ở đây;
   - tạo task `wdog` và task `init`;
   - `vTaskStartScheduler()`.
2. Từ lúc này `wdog` kick độc lập. **Driver SPI4 nhường CPU (`vTaskDelay(1)`) trong lúc chờ flash bận**, nên ngay cả lần format FlashDB đầu tiên (xóa khoảng 1 MB, mất vài giây) cũng không làm lỡ nhịp WDI.
3. Task `init` lần lượt chạy:
   1. `store`;
   2. `syslog`;
   3. `netmgr`. UART3 do `netmgr/lte` tự khởi tạo và sở hữu, không đi qua module `serial`;
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
- **Mongoose:** mỗi `mg_mgr` chỉ được gọi từ task sở hữu nó (`web` hoặc `console`).
  - Dữ liệu tới console đi qua các **MessageBuffer**. MessageBuffer giữ được ranh giới frame, nên mỗi frame UART vẫn thành một WebSocket message như bản ESP32. Mỗi MessageBuffer chỉ có **một task ghi**:
    - log: chỉ task `syslog` ghi, vì mọi dòng log đã đi qua hàng đợi của `syslog`;
    - UART n: chỉ `serial_rx[n]` ghi.

    Buffer đầy thì bỏ frame và tăng bộ đếm.
  - Lệnh "đóng mọi client khi đổi mật khẩu" (F19) đi qua một cờ atomic.
  - `console` rút dữ liệu sau mỗi lần `mg_mgr_poll`, timeout poll 20 ms.
- **coreMQTT:** chỉ task `mqtt` được gọi `MQTT_*`. Task khác đưa dữ liệu vào outbox rồi notify `mqtt`. **Không giữ mutex của outbox trong lúc gọi transport.** `mqtt` lấy tin ra dưới mutex, nhả mutex, rồi mới gửi.
- **Thứ tự khóa** (luôn lấy theo thứ tự này, không bao giờ ngược lại):
  1. mutex bus của cổng;
  2. mutex cache Edge;
  3. mutex `store`;
  4. lock FAL.

  Không giữ mutex bus hay mutex cache Edge trong lúc gọi `store`. Mọi lần chờ mutex đều có timeout, và hết timeout thì ghi log lỗi.
- **Nhiều cổng:**
  - Topic bind cổng qua bitmask `ports`.
  - `websocketTarget` nhận giá trị 0 (tắt), 1, 2 (UART) hoặc 3 (LOG).
  - Thiết bị Edge chọn bus qua trường `uart`.
  - `edgeReport.channel` nhận giá trị 0 (MQTT), 1 hoặc 2 (socket của cổng).
  - Modbus gateway của cổng n chỉ chuyển tiếp ra bus n. IO slave (`io.modbusAddress`) trả lời trên cả hai cổng.

### 6.5 Driver serial

- **Nhận:** DMA vòng (bật cả ngắt HT và TC), kết hợp Receiver Timeout (`RTOR`).
  - Ngắt RTO, IDLE, HT và TC đều được xử lý trong **ISR riêng viết bằng LL**, không qua handler của HAL. Handler của HAL coi `RTOF` là lỗi và hủy DMA RX.
  - Sau mỗi lần timeout, DMA vẫn chạy liên tục. Phải có test xác nhận vẫn nhận được dữ liệu sau nhiều lần timeout liên tiếp.
- **Hai cách cắt frame:**

  | Cách | Áp dụng khi | Quy tắc |
  |---|---|---|
  | Transparent | Bridge transparent, MQTT, console | Kết thúc frame khi đủ `packetLength`, hoặc khi im lặng `packetInterval` ms (0 nghĩa là 3,5 ký tự, tối thiểu 2 ms). Giữ đúng như ESP32 |
  | Modbus RTU | `serial_transact` của Modbus (Edge, Modbus gateway, polling) | `RTOR` đặt t1.5. Khi RTO báo, ISR khởi động một timer phần cứng cho phần còn lại (t3.5 − t1.5). Có ký tự mới trước khi timer hết hạn nghĩa là **vi phạm t1.5**: frame bị đánh dấu và hàm transaction bỏ frame đó (Modbus Serial Line V1.02 §2.5.1.1). Timer hết hạn nghĩa là frame kết thúc |

  Thời gian một ký tự tính theo framing thực: start + data + parity + stop. Với baud > 19200, dùng giá trị cố định t1.5 = 750 µs và t3.5 = 1,75 ms.
- **DMA RX quay vòng mà không có IDLE** (luồng liên tục): ngắt HT/TC vẫn giao dữ liệu. Ghi đè dữ liệu chưa đọc (con trỏ ghi vượt con trỏ đọc) được đếm thành `overrun`, frame đang ráp bị bỏ.
- Lỗi parity, frame hay overrun của USART làm hỏng frame: frame bị bỏ và tăng bộ đếm, như ESP32.
- **Gửi:** DMA, DE phần cứng (`DEAT`/`DEDT`). Mỗi lần phát đều bật DE. UART1 mắc song song RS232 nên điều này vô hại.
- Parity hỗ trợ None/Odd/Even. Mark/Space để lại như bản ESP32: bỏ khỏi UI. XON/XOFF xử lý bằng phần mềm trong driver.
- Baud 600–230400.

### 6.6 Driver ADC (AI1, AI2)

**Mục tiêu:**
- CPU gần như không phải làm gì khi lấy mẫu.
- Mỗi giá trị AI được tính trên một cửa sổ dữ liệu nhất quán.
- Số đọc đạt các tiêu chí định lượng ở cuối mục.

**Cấu hình** (RM0433 Rev 8 §25, DS12556 Rev 8, AN5354):

| Thành phần | Cấu hình |
|---|---|
| ADC | **ADC3** kênh INP0 (PC2_C, AI1) và INP1 (PC3_C, AI2). Single-ended, 16 bit. Nhóm regular gồm 2 rank: rank 1 là INP0, rank 2 là INP1 |
| Clock | **Clock analog 10 MHz, `BOOST` = `01`** (cho ≤ 12,5 MHz). Clock lấy từ PLL2P qua bộ chia của ADC. Đường PLL/prescaler cụ thể ghi trong `.ioc` và trong `board/clock.md`. Không dùng clock cao hơn: AN5354 bảng 12 nêu 19 MHz cho chất lượng 16 bit với LQFP100 |
| Sampling | 64,5 cycle. Một trigger tốn khoảng `2 × 16 × (64,5 + 8,5) / 10 MHz` ≈ 234 µs, nhỏ hơn chu kỳ 1 ms. Ngõ vào ADC nối thẳng ngõ ra LM2904, có tụ 5 pF. Độ settling được **chứng minh bằng bench** (bài kiểm chuyển kênh bên dưới), không chỉ dựa vào lập luận về trở kháng. Nếu không đạt thì tăng lên 387,5 cycle; chu kỳ 1 ms vẫn đủ chỗ |
| Hiệu chuẩn của ADC | Sau khi bật regulator của ADC và trước khi bật ADC: `HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED)`, **kiểm mã trả về**. Lỗi thì AI ở trạng thái failed. Đường khởi động lại khi lỗi (bên dưới) không vào deep-power-down, nên hệ số hiệu chuẩn được giữ. Lần nào ADC rời deep-power-down thì phải hiệu chuẩn lại |
| **Oversampling phần cứng** | `ROVSE` = 1, `OVSR` = 15 (tỷ lệ ×16), `OVSS` = 4 (dịch phải 4 bit), **`TROVS` = 0**: một trigger chạy đủ 16 lần tích lũy, tức `ADC_TRIGGEREDMODE_SINGLE_TRIGGER` trong HAL. `CONT` = 0, `DISCEN` = 0, `LSHIFT` = 0. Giá trị lớn nhất là 16 × 65535 >> 4 = 65535, nên dữ liệu vẫn là 16 bit |
| Trigger | **TIM6_TRGO** (`EXTSEL` = 13), TRGO lấy từ update event, chạy 1 kHz, kích cạnh lên. TIM2 dành cho `now_us()` |
| **DMA** | **BDMA** (DMAMUX2 request 17), circular. Peripheral không tăng địa chỉ, memory tăng. Dữ liệu half-word ở cả hai phía. ADC đặt `DMNGT` = `0b11` (circular DMA) |
| Buffer | `uint16_t adc_buf[2][100][2]`: **hai nửa**, mỗi nửa 100 cặp mẫu = 100 ms, tổng 800 B. Nằm trong region MPU 0x3800FC00–0x3800FFFF: Normal, non-cacheable, XN, được giữ riêng trong linker script. Region này cấu hình trong `Reset_Handler`, trước khi bật DMA (§6.2) |
| Ngắt | **HT và TC của BDMA**, mỗi 100 ms một ngắt, không có ngắt theo từng mẫu. Thêm ngắt lỗi của BDMA và `OVR` của ADC |

**Bàn giao buffer, để mỗi cửa sổ là một snapshot nhất quán:**
- ISR của HT hoặc TC ghi `ready_half` (0 hoặc 1) và tăng `seq`, rồi notify task `io`. Nửa vừa đầy thuộc về CPU trong 100 ms kế tiếp. Trong thời gian đó BDMA đang ghi nửa kia.
- Task `io` (prio 6) cộng 100 mẫu của nửa `ready_half` vào bộ tích lũy kiểu `uint32_t` cho từng kênh. Sau khi cộng, nếu `seq` đã đổi thì nửa đó có thể đã bị ghi đè: bỏ kết quả và tăng `adc_late`. Cộng 200 giá trị mất cỡ micro giây, còn hạn chót là 100 ms.
- Giá trị AI được công bố là trung bình của **cửa sổ 100 ms gần nhất đã hoàn chỉnh**, cập nhật mỗi 100 ms. Vòng 500 ms của `io` chỉ đọc giá trị này. Cửa sổ 100 ms chứa đúng 5 chu kỳ 50 Hz và 6 chu kỳ 60 Hz, nên đó là các điểm triệt lý tưởng. Mức triệt thực tế chịu ảnh hưởng của độ lệch tần số lưới và jitter, và được đo trên bench.

**Đổi sang dòng điện:** `I[mA] = (mean × 3,0) / (65535 × 0,499 × 0,120)`, tính bằng `float`.
- Hệ số 0,499 = 49,9 kΩ / 100 kΩ, shunt 120 Ω, VREF 3,0 V. Ví dụ 20 mA tương ứng 1,1976 V, mã khoảng 26.162.
- Sau đó áp **hiệu chuẩn hai điểm của từng kênh** (gain và offset, đo ở 4 mA và 20 mA).
  - Hệ số hiệu chuẩn nằm trong một *factory record* ở SPI4 0x3F0000, vùng dự trữ ở §5.2. Record có hai bản sao kèm CRC.
  - Record được nạp bằng công cụ sản xuất qua SWD. Factory reset không xóa nó.
  - Chưa có record thì dùng hệ số danh định, và `/api/status` báo `aiCalibrated: false`.
- Edge nhận giá trị bằng µA như bản ESP32.

**Lỗi và phục hồi (F18):**
- `OVR`, lỗi truyền BDMA hoặc hiệu chuẩn lỗi thì **cả AI1 lẫn AI2** bị đánh dấu failed, vì hai kênh dùng chung một scan và một ring. Modbus trả exception 4 như bản ESP32.
- Trình tự phục hồi, chạy trong `io` theo notify từ ISR:
  1. dừng TIM6;
  2. dừng ADC (`ADSTP`) và BDMA;
  3. xóa cờ `OVR` và các cờ lỗi của BDMA;
  4. nạp lại địa chỉ, bộ đếm và thứ tự rank;
  5. bật BDMA, rồi ADC (`ADSTART`);
  6. bật TIM6 **sau cùng**.
- **Chỉ công bố lại giá trị khi đã có đủ 100 cặp mẫu mới**, tức một nửa đầy, cả sau phục hồi lẫn lúc khởi động đầu tiên.

**Tiêu chí nghiệm thu trên bench** (§12.2), áp dụng cho từng kênh sau khi hiệu chuẩn hai điểm, ở 25 °C:

| Hạng mục | Tiêu chí |
|---|---|
| Sai số tại 4, 8, 12, 16 và 20 mA | ≤ ±0,05 mA (khoảng 0,3 % của dải 16 mA; thiết bị tham chiếu ghi 1 % FSR) |
| Độ dao động (độ lệch chuẩn trong 60 s, nguồn dòng ổn định) | ≤ 0,005 mA |
| Nhiễu 50 Hz và 60 Hz, biên độ 1 mA p-p chồng lên 12 mA | Số đọc lệch ≤ 0,05 mA so với khi không có nhiễu |
| Ảnh hưởng chéo khi chuyển kênh | Đổi AI1 giữa 4 và 20 mA thì AI2 (giữ 12 mA) lệch ≤ 0,01 mA, và ngược lại |
| Độ trễ | Bậc thang 4 → 20 mA hiện lên `/api/io` trong ≤ 700 ms |
| Phục hồi | Sau một lỗi được gây ra có chủ đích (`OVR` hoặc dừng BDMA), giá trị được công bố lại trong ≤ 300 ms. Không có giá trị nào được công bố trong lúc phục hồi |
| Tải CPU | Không có ngắt nào theo từng mẫu: số ngắt ADC/BDMA ≤ 10 mỗi giây |

## 7. Mạng

### 7.1 Ethernet

- Driver ETH của HAL H7 dùng descriptor đặt trong SRAM1 non-cacheable. Phát hiện link qua MDIO, poll mỗi 1 s.
- IP tĩnh hoặc DHCP, DNS, hostname: lấy từ cấu hình `network`.
- Bật IGMP để `bridge` dùng multicast.

### 7.2 LTE EC600U

- **Timing của EC600U** lấy theo *Quectel EC600U系列 QuecOpen 硬件设计手册 V1.3* (bản trên developer.quectel.com, 2024-09), mục 3.7–3.8. **Không** dùng timing của EC800 trong bản ESP32 (xung PWRKEY 700 ms ở `modem.c:47`). Mốc thời gian tính từ cạnh GPIO của MCU. Qua transistor, độ trễ không đáng kể so với các giá trị dưới đây.

  | Thao tác | Yêu cầu của tài liệu | Firmware dùng |
  |---|---|---|
  | Bật nguồn: giữ PWRKEY (LTE_PWR = 1 qua Q1) | ≥ 2 s | **2,5 s** |
  | Tắt nguồn bằng PWRKEY | ≥ 3 s | **3,5 s**. Chỉ dùng khi `AT+QPOWD=1` không được |
  | Xung `RESET_N` (LTE_RESET = 1 qua Q2) | ≥ 100 ms | **200 ms** |
  | Chờ UART sẵn sàng sau khi bật hoặc reset | Tài liệu không nêu một con số cố định cho MAIN_UART | Thử `AT` mỗi 500 ms, **deadline 15 s** kể từ lúc nhả PWRKEY hoặc RESET_N. Đo thời gian thật trên bench, rồi đặt deadline bằng 2 lần giá trị đo, nhưng không dưới 10 s |
  | Sau khi tắt nguồn, chờ trước khi bật lại | Theo mục 3.7 | **2 s** kể từ khi AT không còn phản hồi. Board không nối chân `STATUS` |

  Không kết luận "modem hỏng" chỉ vì modem không phản hồi ngay sau xung nguồn.
- **Khởi động:**
  1. Thử `AT` trong 3 s, vì sau khi gateway restart modem thường vẫn đang bật. Không có phản hồi thì nhấn PWRKEY theo bảng trên rồi chờ UART sẵn sàng.
  2. Vẫn không phản hồi thì kích `RESET_N`. Board này có chân reset, GW100 thì không.
  3. Đồng bộ AT, đọc IMEI, ICCID và phiên bản firmware của module (`AT+QGMR`, ghi vào log).
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
     - Buffer frame mỗi DLC ≥ 2048 + 7 byte. Overhead gồm 2 cờ, address, control, 2 byte độ dài và FCS. Cấp 2112 B trong AXI.
     - **PPP là luồng byte, không gói vào từng frame.** Sau khi escape (RFC 1662 §4.2), một gói PPP có thể dài gần gấp đôi, nên luồng PPP đi ra được cắt thành các frame UIH ≤ N1 trên DLC1. Chiều nhận ghép payload UIH lại thành luồng byte. Không giả định một frame chứa đúng một gói PPP.
     - Mở DLC0, rồi DLC1 và DLC2 bằng SABM, chờ UA trong khoảng T1, gửi lại tối đa N2 lần.
     - Đóng mux bằng lệnh CLD.
- **Kênh CMUX:**
  - DLC0: kênh điều khiển.
  - DLC1 cho PPP: dữ liệu đi vào `pppos_input_tcpip`.
  - DLC2 cho AT: `AT+CSQ` và `AT+QNWINFO` mỗi 20 s.
- **State machine CMUX** (`proto/cmux.c`, C thuần, theo 3GPP TS 27.010 §5):

  | Trạng thái | Sự kiện | Hành động, trạng thái kế tiếp |
  |---|---|---|
  | `AT` | `AT+CMUX=…` trả `OK` | Gửi SABM cho DLC0 → `OPEN_CTRL` |
  | `AT` | Lệnh bị từ chối | → `PLAIN_PPP` (đường dự phòng) |
  | `OPEN_CTRL` | Nhận UA của DLC0 | Gửi SABM cho DLC1 rồi DLC2 → `OPEN_DLC` |
  | `OPEN_*` | Hết T1 (200 ms) | Gửi lại SABM, tối đa N2 = 3 lần. Quá N2 → `RECOVER` |
  | `OPEN_DLC` | Đã nhận UA của DLC1 và DLC2 | → `RUNNING`: bắt đầu PPP trên DLC1, gửi AT trên DLC2 |
  | `RUNNING` | Nhận DM hoặc DISC | Kênh tương ứng đóng → `RECOVER` |
  | `RUNNING` | Nhận **FCoff** hoặc **FCon** trên DLC0 | Dừng hoặc tiếp tục gửi trên **toàn bộ mux**, mọi DLC (27.010 §5.4.6.3.5–6). Trả lời bằng response tương ứng |
  | `RUNNING` | Nhận **MSC** trên DLC0 | Đọc bit FC trong tín hiệu V.24 để dừng hoặc tiếp tục gửi trên **đúng DLC** được nêu. Trả lời bằng MSC response |
  | `RUNNING` | Nhận Test command hoặc lệnh không hỗ trợ | Trả Test response, hoặc NSC (Non Supported Command) |
  | `RUNNING` | Không nhận được frame nào từ modem trong 60 s, và `AT` trên DLC2 không phản hồi | → `RECOVER` |
  | `RECOVER` | | Gửi CLD (đóng mux), chờ 1 s, kích `RESET_N`, quay về `AT` |

  Frame lỗi (FCS sai, sai cờ `0xF9`) bị bỏ, kèm bộ đếm. Bộ ráp frame **tái đồng bộ** ở cờ `0xF9` tiếp theo.
- **Dự phòng PPP thẳng:** dùng khi `AT+CMUX` bị từ chối. Thông tin tín hiệu khi đó chỉ được đọc trước khi PPP lên, giống bản ESP32. Muốn gửi AT để phục hồi thì phải thoát data mode bằng `+++` (im lặng 1 s trước và sau) rồi `ATH`. Không thoát được thì kích `RESET_N`.
- **Đường truyền UART3:** 115200 baud, không có RTS/CTS.
  - RX dùng DMA vòng 4 KB với ngắt HT, TC và IDLE, do `cmux_rx` (prio 25) rút ra (§6.1). Ở 115200 baud, 4 KB tương đương khoảng 350 ms dữ liệu.
  - **Khi `pppos_input_tcpip` trả lỗi** (`ERR_MEM`): thử lại tối đa 3 lần, mỗi lần cách 1 ms. Ở 115200 baud, 1 ms là khoảng 12 byte, nên ring vẫn dư. Hết 3 lần thì bỏ đoạn dữ liệu đó và tăng `ppp_in_drop`. PPP tự phục hồi qua FCS và cơ chế gửi lại của TCP.
  - Khi đang FCoff, dữ liệu PPP đi ra được giữ trong hàng đợi gửi của DLC1, tối đa 4 KB. Đầy thì báo `ERR_MEM` ngược lên lwIP.
  - Tràn ring được đếm thành `overrun`, và PPP tự phục hồi bằng cơ chế của nó (FCS, gửi lại TCP).
  - **Cần chứng minh bằng tải liên tục** trên bench: tải xuống HTTPS lớn trong lúc DLC2 vẫn đọc tín hiệu. Số `overrun` phải bằng 0.
- **Phục hồi:** PPP không có địa chỉ quá 2 phút thì gửi `AT+CFUN=1,1` qua DLC2. Không phản hồi AT thì vào `RECOVER`.
- **PPP:** xác thực None/PAP/CHAP/tự thương lượng, APN và DNS lấy từ `network`.
- **Ưu tiên kết nối (`network.priority`)** giữ nguyên như ESP32:
  - 0: chỉ Ethernet;
  - 1: ưu tiên Ethernet;
  - 2: ưu tiên LTE.

  Default route chuyển sang link kia khi link đang ưu tiên mất địa chỉ (`netmgr.c:34`).
  - **Giới hạn, giữ như ESP32:** firmware không phát hiện trường hợp mất Internet trong khi link vẫn còn địa chỉ.
  - DNS dùng server của link đang giữ default route.
  - Khi đổi route, các kết nối TCP/TLS đang mở (bridge client, MQTT, Modbus TCP) bị đóng và kết nối lại qua link mới.
- Các chuỗi hiển thị trên UI đổi từ EC800 sang EC600U.

### 7.3 TLS

- `tlsconn.c` chép từ ESP32, dùng chung cho `bridge[1]`, `bridge[2]` (TCP client và HTTPS) và MQTT.
- Giữ nguyên:
  - TLS 1.2;
  - `tlsVerify` 0/1/2;
  - SNI;
  - deadline cho handshake và cho thao tác ghi;
  - kiểm chứng chỉ khi upload (F20).
- `mbedtls_config.h`, dùng **mbedTLS 3.6 LTS** (khóa phiên bản cụ thể trong `third_party`):
  - `MBEDTLS_SSL_IN_CONTENT_LEN` 16384, `OUT` 4096;
  - `MBEDTLS_HAVE_TIME_DATE`, với hàm giờ trả về giờ hệ thống;
  - entropy từ RNG phần cứng (`MBEDTLS_ENTROPY_HARDWARE_ALT`);
  - cipher suite: chỉ ECDHE kèm AES-GCM/CHACHA20 và RSA kèm AES-GCM, chỉ TLS 1.2.
- **Khi chưa có giờ** (§10.4), áp dụng cho mọi kết nối TLS kể cả MQTT, **khi `tlsVerify` ≥ 1**:
  - chỉ bỏ qua hai cờ lỗi về ngày (`MBEDTLS_X509_BADCERT_EXPIRED` và `_FUTURE`) và ghi cảnh báo;
  - chuỗi chứng chỉ, CA và tên host vẫn kiểm đủ;
  - khi đã có giờ, mọi handshake sau đó kiểm ngày đầy đủ.

  Với `tlsVerify` = 0, kết nối chủ động không kiểm chứng chỉ, như ESP32.

## 8. MQTT

- **Thư viện:** coreMQTT v2.3.1.
- **Adapter transport** (`services/mqtt_transport.c`) đặt trên `tlsconn` (khi có TLS) hoặc socket thường. Adapter **đổi mã trả về** cho đúng hợp đồng của coreMQTT (`transport_interface.h`), vì `tls_read` của ESP32 trả `TLS_AGAIN` = −2 khi chưa có dữ liệu và trả 0 khi kết nối đã đóng:

  | Nguồn | Trả cho coreMQTT |
  |---|---|
  | Có dữ liệu (n > 0) | n |
  | `TLS_AGAIN`, `EWOULDBLOCK` | 0 (chưa có dữ liệu) |
  | Kết nối đóng (0, close-notify) | −1 |
  | Lỗi | −1 |

  Ghi một phần thì trả số byte đã ghi. Có host test cho mọi trường hợp: đọc một phần, AGAIN, EOF.
- **Vá MQTT 3.1:** CONNECT dùng protocol name `MQIsdp`, level 3. Bản vá nằm ở `third_party/patches/coremqtt-mqtt31.patch`. Host test kiểm từng byte, gồm cả Remaining Length. Bench kiểm với mosquitto ở cả 3.1 và 3.1.1: CONNACK, Client ID, **ngắt kết nối ở từng pha QoS 1/2** (trước PUBACK, trước PUBREC, sau PUBREL trước PUBCOMP) khi `cleanSession` = 0 và = 1, kiểm không mất và không nhân đôi tin QoS 2.
- **Outbox** (`services/mqtt_outbox.c`, đặt trong SRAM2+3), mỗi tin có một trạng thái:

  | Trạng thái | Ý nghĩa | Chuyển tiếp |
  |---|---|---|
  | `QUEUED` | Chưa gửi, chưa cấp packet id | Gửi đi → QoS 0: xóa luôn; QoS 1: `WAIT_PUBACK`; QoS 2: `WAIT_PUBREC` |
  | `WAIT_PUBACK` | Đã gửi PUBLISH QoS 1 | PUBACK → xóa, giải phóng packet id |
  | `WAIT_PUBREC` | Đã gửi PUBLISH QoS 2 | PUBREC → gửi PUBREL → `WAIT_PUBCOMP` |
  | `WAIT_PUBCOMP` | Đã gửi PUBREL | PUBCOMP → xóa, giải phóng packet id |

  - **Gửi lại chỉ diễn ra khi kết nối lại** (MQTT 3.1.1 §4.4), không theo chu kỳ:
    - **"Phiên được giữ"** được xác định khác nhau theo phiên bản:
      - **3.1.1:** theo cờ Session Present trong CONNACK.
      - **3.1:** CONNACK của 3.1 không có cờ này (byte đó là reserved), nên coi là phiên được giữ **khi và chỉ khi `cleanSession` = 0**.

      Bản vá coreMQTT (`coremqtt-mqtt31.patch`) đặt `sessionPresent` theo đúng quy tắc này trước khi `MQTT_Connect()` chạy `handleSessionResumption`.
    - **Ai giữ trạng thái gì:**
      - coreMQTT giữ bảng packet id và trạng thái QoS, qua `MQTT_InitStatefulQoS` với mảng 16 phần tử cho mỗi chiều. Nó tự gửi lại **PUBREL** khi phiên được giữ.
      - Outbox giữ payload. Outbox tự gửi lại **PUBLISH**: `MQTT_Publish` với `dup = true` và đúng packet id cũ.
      - Hai bên đồng bộ qua callback sự kiện của coreMQTT (PUBACK, PUBREC, PUBCOMP).
    - **Phiên được giữ:** gửi lại PUBLISH của các tin `WAIT_PUBACK` và `WAIT_PUBREC` với DUP và packet id cũ. PUBREL của các tin `WAIT_PUBCOMP` do coreMQTT gửi lại.
    - **Phiên mới** (clean session, hoặc 3.1.1 trả Session Present = 0): các tin đang dở quay về `QUEUED`, cấp packet id mới, và trạng thái QoS của coreMQTT được xóa.
    - **Luôn đăng ký lại toàn bộ subscribe** sau mỗi lần kết nối, ở cả hai trường hợp. SUBSCRIBE lặp lại là vô hại.
  - `retryInterval` vẫn chỉ là **khoảng thời gian giữa hai lần thử kết nối lại**, như ESP32.
  - **Hết hạn sau 1 giờ chỉ áp dụng cho tin `QUEUED`.** Tin đang ở giữa giao dịch QoS không bao giờ bị hết hạn, vì packet id của nó vẫn đang được sử dụng.
  - Tổng dung lượng giới hạn 64 KB. Đầy thì bỏ tin mới và ghi cảnh báo.
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
  - chỉ ghi khi lưu cấu hình. Ngoại lệ là bộ đếm boot `st.boot`, ghi một lần nhỏ ở mỗi lần boot để log có dạng `[boot N …]` (`store.c:219`);
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

### 9.3 Arena JSON và buffer văn bản Edge

Có hai vùng tĩnh riêng trong AXI, vì chúng có vòng đời khác nhau.

**Arena JSON, 64 KB:**
- Là một **bộ cấp phát có thu hồi** (TLSF, mã nguồn mở, đặt trong `third_party/tlsf`) chạy trên vùng tĩnh. Bộ cấp phát được khởi tạo lại sau mỗi phạm vi, nên không phân mảnh lâu dài.
  - Đây phải là bộ cấp phát có thu hồi, không dùng cấp phát tuyến tính. `cfgmodel` tạo defaults, nhân bản rồi thay field, và cJSON khi in cũng cấp phát trung gian. Với cấp phát tuyến tính, những block đã giải phóng vẫn chiếm chỗ.
  - Với TLSF, đỉnh bộ nhớ thực tế bằng đỉnh của các cấp phát còn sống cộng phần phân mảnh trong một phạm vi. Host test đo đúng con số này bằng cùng bộ TLSF (§12.1).
- **Phạm vi luôn đồng bộ:** `arena_begin()` và `arena_end()` được gọi bên trong **một** callback của Mongoose, hoặc bên trong một bước khởi động của `init`.
  - Không bao giờ giữ arena qua lần quay lại event loop, nên nhiều request trong cùng task `web` không thể dùng lẫn arena của nhau.
  - `arena_begin()` kiểm không lồng nhau và đúng task (`configASSERT`).
- **Hết bộ nhớ trong arena** thì thao tác trả lỗi `Out of memory.` (HTTP 500) và không ghi gì xuống flash. Không bao giờ lưu một cấu hình chuẩn hóa bị thiếu trường.
- **Dữ liệu sống lâu hơn phạm vi phải chép ra heap hoặc vùng tĩnh trước `arena_end()`.** Ví dụ:
  - template report đã biên dịch: `edge_tmpl_compile` giữ `t->text` từ `cJSON_PrintUnformatted` và các con trỏ `frag[]` trỏ vào đó (`edgecore.c:358`). Bản STM32 biên dịch template ngoài phạm vi arena, tức cấp từ heap;
  - tên điểm và chuỗi trong struct lúc chạy;
  - con trỏ cấu hình.

  Host test **poison** toàn bộ arena ở `arena_end()` (`ASAN_POISON_MEMORY_REGION`), nên mọi truy cập sau khi phạm vi kết thúc đều bị phát hiện.

**Buffer văn bản Edge, 48 KB:** chứa văn bản `edgeDevices`.
- Dùng khi nhận body của `POST edgeDevices`, qua nhiều callback.
- Dùng khi nạp văn bản chuẩn hóa lúc boot.
- Tại mỗi thời điểm chỉ có **một chủ sở hữu**: một connection, hoặc task `init`.
  - Một `POST edgeDevices` thứ hai đến trong lúc buffer đang bận thì nhận ngay 503 `{ "error": "Another Edge save is in progress." }`.
  - Buffer được giải phóng khi request xong, khi connection đóng, hoặc khi body hết thời gian chờ (quy tắc chung ở §10.1).

**Các cấu hình nhỏ** (port, mqtt*, system, io, edgeReport, edgeLinkage):
- body ≤ **10 KB**, nằm sẵn trong `c->recv`. **Văn bản chuẩn hóa cũng phải ≤ 10 KB**, kiểm trước khi ghi; vượt quá thì trả 413 `{ "error": "These settings are too large." }`. Nhờ vậy cái gì `GET` trả về đều `POST` lại được. Host test tính văn bản chuẩn hóa lớn nhất của từng cấu hình theo schema: mọi chuỗi dài tối đa và toàn ký tự phải escape (ví dụ `mqttPublish` có `identifier` toàn `\\`), rồi kiểm ≤ 10 KB và kiểm vòng `POST → GET → POST`;
- toàn bộ parse, validate và in ra chạy trong một phạm vi arena.
- Ước tính `mqttPublish` 16 topic đầy đủ (khoảng 4 KB văn bản) có đỉnh khoảng 46 KB, lấy theo tỷ lệ cây/văn bản khoảng 5,7 đã đo. Host test chốt con số thật.

### 9.4 Lưu `edgeDevices`

**Giới hạn kích thước** (đo bằng `cfgmodel` hiện có):
- Mô hình 100 điểm gửi lên ở dạng gọn chỉ khoảng 11–16 KB.
- Sau khi điền mặc định, văn bản lên **26–36 KB**, vì `cfg_validate` điền đủ mọi trường của thiết bị và node. UI có thể gửi mọi trường.
- Vì vậy cả hai giới hạn là **48 KB**:
  - body ≤ 48 KB;
  - văn bản chuẩn hóa ≤ 48 KB, kiểm trong lúc ghi bản ghi. Vượt quá thì trả 413 `{ "error": "The Edge model is too large." }` và không đổi generation.

  Nhờ vậy cái gì `POST` đã nhận thì `GET` và lần boot sau đều đọc lại được.

**Nhận:**
1. Ở `MG_EV_HTTP_HDRS`:
   - thiếu `Content-Length`, hoặc request dùng `Transfer-Encoding: chunked`: trả 411;
   - `Content-Length` > 48 KB: trả 413;
   - buffer văn bản Edge đang bận: trả 503.
2. Nếu hợp lệ, handler nhận quyền kiểm soát connection và chép body vào buffer văn bản Edge. Số byte **thực nhận** cũng được kiểm theo giới hạn.
3. Khi đủ body, phần còn lại chạy **đồng bộ trong một callback**, trong một phạm vi arena.

**Validate theo đúng thứ tự của `cfgmodel.c`** (`slaves()` dòng 478 và `slaves_strict()` dòng 413), để câu báo lỗi đầu tiên giống bản ESP32. `proto/jsonscan.c` duyệt văn bản đã nằm trong RAM, nên có thể chạy nhiều lượt mà không cần parse cả cây:

| Pha | Việc làm (theo thứ tự thiết bị, rồi thứ tự node) |
|---|---|
| 1. Header | Validate header của **mọi** thiết bị theo `ITEM_SLAVE` (tương đương `list()`, tối đa **64 thiết bị**) |
| 2. Cấu trúc | Với từng thiết bị: tối đa 1 IO và 1 VIRTUAL, đổi tên cố định `SBIOT_IO`/`SBIOT_VIRTUAL`, `name_ok`, tên dành riêng, **trùng tên thiết bị**, tối đa **5 thiết bị Modbus TCP**. Rồi với từng node: tổng ≤ 100, validate theo `ITEM_NODE`, quy tắc kênh IO và điểm ảo (`fc`, `reg`, `dtype`), `name_ok`, **trùng tên điểm** |
| 3. Strict (chỉ khi lưu) | Với từng thiết bị: địa chỉ DL/T645 12 chữ số hoặc slave ID 1–255. Slave ID được map hoặc của thiết bị ảo không trùng với ID mà **bất kỳ** thiết bị nào khác đang trả lời, kể cả thiết bị không map (`exposed_unit`). Với từng node: `di` 8 hex, `fc`/`dtype` khớp nhau, thanh ghi không vượt 65535, khoảng đã map không vượt 65535 và **không chồng lên bất kỳ node nào được expose** trong cùng thiết bị và cùng không gian function code, kể cả node không map |

- **Bảng tóm tắt** (8 KB, cấp phát cố định trong arena) giữ những gì pha 3 cần:
  - mỗi thiết bị: chỉ số, `exposed_unit`, cờ map;
  - mỗi node: chỉ số thiết bị, `fc`, `start`, `width`, `mapEn`;
  - tên thiết bị và tên điểm (mỗi tên ≤ 17 B), giữ trong hai bảng riêng vì đây là hai phạm vi kiểm trùng khác nhau.

  Hash FNV-1a chỉ dùng để tìm nhanh. **Hai tên được coi là trùng chỉ khi so `strcmp` cũng bằng nhau.**
- **Khi nạp lúc boot** (`cfg_load`) chạy pha 1 và 2 mà không chạy pha 3, như ESP32. Một mô hình đã lưu bởi firmware cũ vẫn chạy được.
- **Mỗi node chỉ sống trong một phạm vi con:** đánh dấu, parse, validate, in, rồi giải phóng. Vì vậy đỉnh bộ nhớ không tăng theo số node.

**Ghi:**
- Văn bản chuẩn hóa được ghi theo đúng thứ tự của cJSON nguyên khối: các field của thiết bị theo thứ tự schema, `"nodes":[…]` ở cuối, vì `slaves()` thêm `nodes` sau `list()`.
- Văn bản được nối vào bản ghi 8 KB hiện hành `ed.<g>.<i>`, với `g = gen + 1`. Định dạng này giữ nguyên như ESP32.

**Commit và dọn dẹp** (FlashDB không có giao dịch nhiều key, nên tính nguyên tử dựa vào thứ tự sau):
1. Ghi đủ các bản ghi `ed.<g>.*`.
2. Ghi manifest `edgeDevices = @<g>,<count>`. **Đây là điểm commit.**
3. Xóa các key `ed.<gen>.*` của generation cũ, như `store.c:141`. Nếu đang có `GET` đọc generation cũ thì chờ `GET` đó xong mới xóa (xem bên dưới).
4. **Lúc boot**, xóa mọi key `ed.*` không thuộc generation của manifest. Đó là bản ghi mồ côi do mất điện giữa bước 1 và 2, hoặc giữa bước 2 và 3.

Mất điện ở bất kỳ bước nào thì manifest vẫn trỏ tới một generation đầy đủ.

**Đọc (`GET`):**
- `GET` **ghim** generation đọc được từ manifest lúc bắt đầu, bằng một bộ đếm người đọc của `store`.
- Gửi tuần tự các bản ghi của generation đó bằng `mg_http_write_chunk`. Chỉ gửi bản ghi kế tiếp khi `c->send.len` < 4 KB.
- Xong, hoặc connection đóng, thì bỏ ghim.
- Có test: `GET` chạy xen với `POST`, và trong lúc FlashDB đang dọn rác.

**Khởi động:**
1. Đọc văn bản chuẩn hóa (≤ 48 KB) vào buffer văn bản Edge.
2. Chạy `cfg_load` theo node (pha 1 và 2).
3. Dựng mô hình lúc chạy bằng struct C cấp phát lâu dài: thiết bị, node, chỉ mục FNV-1a.
4. Biên dịch template report, cấp từ heap.
5. Giải phóng buffer văn bản Edge.

**Buffer report:**
- Kích thước tính bằng `edge_tmpl_size()` của mã ESP32 (`edgecore.c:407`): mỗi slot giá trị được dành 242 B, cho trường hợp xấu nhất là một chuỗi đã escape. Vì vậy kích thước phụ thuộc **số slot** của template, không phụ thuộc số điểm.
- Giới hạn là **26 KB**, tức khoảng 100 slot với template 2 KB. Buffer cấp từ heap lúc boot, và ngân sách AXI ở §5.3 đã tính khoản này.
- Template cần nhiều hơn thì lúc boot Edge báo trạng thái `TEMPLATE TOO LARGE` và tắt report, cùng cách bản ESP32 xử lý khi thiếu bộ nhớ cho report (`edge_rt.c:880`).
- Lúc lưu `edgeReport`, `web` biên dịch thử template với tên điểm của mô hình Edge đang chạy, và trả 4xx `{ "error": "The report template needs too many values for this gateway." }` nếu vượt giới hạn. Nếu tên không khớp với mô hình đang chạy (ví dụ vừa lưu `edgeDevices` nhưng chưa restart), lần kiểm lúc boot là lần quyết định.
- Cấu hình cực đại này (100 slot) có mặt trong kịch bản đo bộ nhớ (§12.3).

**Ràng buộc kiểm thử:** với mọi payload trong bộ test, kể cả payload có nhiều lỗi, thứ tự thiết bị và node bị đảo, hay tên trùng hash, kết quả xử lý theo node phải **giống từng byte** với `cfg_validate` nguyên khối: cùng văn bản chuẩn hóa, cùng câu báo lỗi đầu tiên (§12.1).

## 10. Web, API và UI

### 10.1 Mongoose

- `MG_ARCH` là FreeRTOS kèm socket lwIP, `MG_TLS` = none. Hai `mg_mgr` chạy trong hai task (§6.1).
- **Body:**
  - Với `/api/ota`, `/api/cert` và `POST edgeDevices`: xử lý ở `MG_EV_HTTP_HDRS`, tự đọc `c->recv` theo luồng.
    - Các endpoint này bắt buộc có `Content-Length`. Request dùng chunked thì trả 411.
    - Giới hạn áp lên số byte thực nhận: OTA ≤ kích thước slot, chứng chỉ ≤ 12 KB, Edge ≤ 48 KB.
  - Các endpoint khác: giới hạn body 10 KB (§9.3), vượt quá thì trả 413. `MG_MAX_RECV_SIZE` đặt ở mức tương ứng.
- **Giữ hành vi của bản ESP32:**
  - UI gzip nhúng trong firmware, phục vụ không cần xác thực, ETag là hash nội dung từng file (F23);
  - Basic auth cho `/api/*`, so sánh thời gian hằng định;
  - trả 401 **không kèm** `WWW-Authenticate`;
  - đổi login có hiệu lực ngay và đóng mọi client WebSocket;
  - **Quy tắc timeout chung cho mọi body nhận theo luồng** (OTA, chứng chỉ, Edge): bỏ cuộc sau **hai** lần chờ 15 s liên tiếp không có byte nào, tức 30 s im lặng. Kết quả là 408 và giải phóng mọi buffer.
- **WebSocket console** tại `/ws?auth=`:
  - kiểm login **trước** handshake, sai thì trả 401;
  - tối đa 4 client;
  - chế độ LOG nhận lệnh `help`, `status`, `restart`;
  - chế độ UART1 hoặc UART2 chuyển dữ liệu hai chiều dưới dạng binary.

### 10.2 Thay đổi API so với bản ESP32

| Mục | Thay đổi |
|---|---|
| `GET /api/device` | `portmax` = 2. `io` = `{do: 2, di: 2, ai: 2, ao: 0}`. Model và phần cứng là `SBIOT-H750` |
| `/api/config/port2` | Schema đã có từ ESP32 nhưng bị tắt theo phần cứng. Nay được **bật trên phần cứng STM32**. TLS có trên cả hai cổng |
| `/api/cert?scope=` | Thêm `port2` |
| `port<n>` | Bỏ `rfc2217` |
| `system` | Bỏ `telnet`, `telnetPort` |
| `edgeDevices` | Tối đa 100 điểm, 64 thiết bị. Body ≤ 48 KB, văn bản chuẩn hóa ≤ 48 KB. Lỗi 411, 413, 503 như §9.4 |
| Các cấu hình khác | Body và văn bản chuẩn hóa ≤ 10 KB (bản ESP32: body 96 KB) |
| `POST /api/ota` | Thêm 409 khi đang ở `TRIAL` (§11.3) |
| `GET /api/status` | Thêm trạng thái của port2. Thêm `wdiMaxGapMs`. Thêm `mem` gồm: <br>• heap AXI: còn trống hiện tại và thấp nhất; <br>• đỉnh arena; <br>• mức chiếm dụng cao nhất của các pool tĩnh (pbuf, netconn, ring, outbox, MessageBuffer) dưới dạng `{used, size}`; <br>• stack còn dư thấp nhất cùng tên task; <br>• các bộ đếm `overrun` và frame bị bỏ của từng UART |
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
| `size` | Kích thước phần thân, tính từ offset 0x400. Header cộng thân ≤ 1.472 KB |
| SHA-256 | Tính trên **phần thân**, đúng `size` byte từ offset 0x400 |
| CRC32 | Tính trên các trường phía trước trong header |

- Vector table nằm ở đầu phần thân, tại 0x90000400.
- `tools/mkimage.py` tạo header sau khi build.
- **Trước khi nhảy vào app**, bootloader kiểm:
  - MSP ban đầu nằm trong DTCM hoặc AXI;
  - reset vector nằm trong khoảng [0x90000400, 0x90000400 + `size`) và có bit Thumb.

### 11.2 Bootloader (flash nội, đơn luồng)

**Bản ghi trạng thái** (SPI4 0x100000, slot A ở sector 4 KB đầu, slot B ở sector 4 KB kế tiếp):
- Mỗi bản ghi chứa:
  - `seq` (uint32);
  - `state`;
  - `offset` (checkpoint, tính theo đơn vị 64 KB);
  - `trial`;
  - `attempts`: số lần đã **bắt đầu** checkpoint `offset` của trạng thái hiện tại mà chưa xong, kèm `verify_fail`: số lần VERIFY hoặc kiểm SHA sau RESTORE thất bại;
  - `degraded`: lần boot trước không ghi được bản ghi trạng thái;
  - bản sao header của image trong staging và của image trong backup;
  - lý do lần chuyển trạng thái gần nhất;
  - **CRC32 ở cuối**, ghi sau cùng.
- **Cách ghi:**
  1. Chọn slot không chứa bản ghi hợp lệ mới nhất.
  2. Xóa sector đó.
  3. Ghi thân bản ghi, rồi ghi CRC sau cùng.

  Bản ghi cũ vẫn hợp lệ cho tới khi bản ghi mới ghi xong. Ghi dở thì CRC sai, nên bản ghi đó không được dùng.
- **Cách đọc:** lấy bản ghi có CRC đúng và `seq` mới hơn theo phép so sánh có quay vòng: `(int32_t)(a − b) > 0`. Flash trắng (`0xFF…`) không phải bản ghi hợp lệ.
- **Không bản ghi nào hợp lệ** (lần đầu sau khi nạp qua SWD, hoặc cả hai đều hỏng):
  - image QSPI hợp lệ: coi như `CONFIRMED`, ghi bản ghi mới;
  - image QSPI hỏng và backup hợp lệ: chuyển sang `RESTORE`;
  - cả hai đều hỏng: vào chế độ cứu hộ.

**Các trạng thái:**

| Trạng thái | Bootloader làm gì | Chuyển sang |
|---|---|---|
| `CONFIRMED` | Kiểm SHA-256 của image QSPI | Hợp lệ: nhảy vào app. Hỏng: `RESTORE` nếu backup hợp lệ, ngược lại vào cứu hộ |
| `PENDING` (app ghi khi staging đã đầy đủ) | **Kiểm header, kích thước và SHA của staging trước khi xóa bất cứ thứ gì** | Hợp lệ: `BACKUP(0)`, hoặc thẳng `WRITE(0)` nếu image QSPI hiện tại hỏng (khi đó giữ backup cũ). Hỏng: `CONFIRMED`, kèm lý do |
| `BACKUP(off)` | Chép QSPI → backup từ checkpoint `off` | Xong: **kiểm SHA của backup** khớp header của image đang chạy, rồi `WRITE(0)`. Không khớp: làm lại `BACKUP(0)` một lần, lần hai vẫn sai thì `CONFIRMED` (hủy cài) |
| `WRITE(off)` | Chép staging → QSPI từ checkpoint `off` | Xong: `VERIFY` |
| `VERIFY` | SHA của QSPI so với header của staging | Khớp: `TRIAL(0)`. Không khớp: làm lại `WRITE(0)` một lần, lần hai vẫn sai thì `RESTORE(0)` |
| `TRIAL(n)` | `n + 1`, ghi bản ghi, rồi nhảy vào app | `n + 1 > 3`: `RESTORE(0)`. App xác nhận: `CONFIRMED`. App yêu cầu rollback: `RESTORE(0)` |
| `RESTORE(off)` | Ở `off = 0`: **kiểm SHA của backup trước khi xóa QSPI**. Rồi chép backup → QSPI từ `off`. **Backup là nguồn không đổi** của trạng thái này: không bao giờ chép vào backup khi đang `RESTORE` | Xong và SHA của QSPI khớp: `CONFIRMED`. **Xong mà SHA không khớp:** `verify_fail + 1`, làm lại `RESTORE(0)`. `verify_fail` ≥ 2: cứu hộ. Backup hỏng ngay từ đầu: nếu image QSPI còn hợp lệ thì chạy nó (`CONFIRMED`, ghi lý do), nếu không thì vào cứu hộ |

**Quy tắc chung:**
- **Kiểm image trước mọi lần nhảy**, kể cả ở `CONFIRMED` và `TRIAL`, không có ngoại lệ. Phải đạt cả: magic, CRC32 của header, mã phần cứng, `size` ≤ slot, SHA-256 của phần thân, MSP và reset vector (§11.1).
  - Ở `TRIAL`, kiểm thất bại thì chuyển `RESTORE(0)`, không bao giờ nhảy vào image hỏng.
- **Bộ đếm số lần thử được lưu qua reset:**
  - Trước khi bắt đầu một checkpoint, bootloader ghi bản ghi với `attempts + 1`.
  - Xong checkpoint thì ghi `offset + 1, attempts = 0`.
  - Vì vậy một lần treo hay reset giữa chừng đã được đếm trước khi nó xảy ra.
  - `attempts` > 3 tại cùng một checkpoint: với `BACKUP`, hủy cài (`CONFIRMED`, giữ image đang chạy); với `WRITE` và `RESTORE`, vào cứu hộ.
- **SPI4 lỗi** (đọc lỗi hoặc quá deadline khi ghi bản ghi):
  - nếu image QSPI hợp lệ: chạy nó ở chế độ `degraded`, không đổi trạng thái, app ghi cảnh báo vào log và **từ chối OTA** (409);
  - nếu image QSPI không hợp lệ: cứu hộ.
- Mỗi checkpoint 64 KB gồm: xóa các sector 4 KB của đích (16 sector, riêng checkpoint cuối làm tròn theo §5.2), ghi theo page, đọc lại so sánh với nguồn, rồi ghi `offset + 1`.
  - Mất điện giữa chừng thì lần boot sau làm lại đúng checkpoint đó.
  - Nguồn chép không bao giờ bị đụng tới: staging trong `WRITE`, QSPI trong `BACKUP`, backup trong `RESTORE`.
- **Watchdog:** bootloader kick WDI trong mọi vòng lặp, nhưng mỗi thao tác flash có **deadline** bằng 2 lần thời gian tối đa trong datasheet GD25Q32E (xóa sector, ghi page, ghi thanh ghi trạng thái).
  - Hết deadline thì **ngừng kick**, để SGM706 reset board.
  - Lần boot sau làm lại checkpoint đang dở. Số lần thử được đếm bằng `attempts`, theo quy tắc ở trên.
  - Như vậy watchdog không thể bị "nuôi" vô hạn khi flash bị kẹt.
- **Chế độ cứu hộ:** LED WORK nháy nhanh, chỉ nạp lại được qua SWD.

**Trình tự mỗi lần boot:**
1. **Kick WDI.**
2. **Ngõ ra an toàn:**
   1. OE = 1, để 74HC573 đưa ngõ ra về hi-Z; R110/R113 kéo base của transistor xuống nên relay nhả;
   2. D1/D2 = 0;
   3. xung LE;
   4. OE = 0.

   Ngoài ra LTE_PWR = LTE_RESET = 0 và ETH_RESET giữ ở mức reset.

   **Cửa sổ không được bảo đảm trên board hiện tại:** từ lúc cấp nguồn, hoặc từ lúc MCU reset trong khi latch vẫn được cấp điện, cho tới bước 2.1 (vài ms), ngõ ra của latch có thể ở trạng thái cũ hoặc bất định, vì R103 kéo OE xuống GND.
3. Cấu hình clock và QSPI (§6.2), SPI4.
4. Đọc bản ghi trạng thái và chạy state machine ở trên.
5. Bàn giao cho app theo §6.2.

Logic trạng thái và cài đặt viết trong `boot/install.c` bằng C thuần, với giao diện flash trừu tượng, để chạy host test có cắt điện (§12.1).

**Đề xuất cho phần cứng (không bắt buộc):** đổi R103 thành kéo lên 3V3, để đóng cửa sổ nêu ở bước 2.

### 11.3 OTA phía ứng dụng

`POST /api/ota` chạy trong task `web`:
- **Đang ở `TRIAL` thì từ chối** với 409 `{ "error": "The running firmware is not confirmed yet; try again in a minute." }`. Nếu không chặn, pha BACKUP sẽ chép đè một image chưa được xác nhận lên bản backup vẫn đang chạy tốt.
- Nhận theo luồng vào staging. Sector 4 KB được xóa ngay trước khi ghi tới.
- Kiểm header ngay từ khối đầu tiên: magic, mã phần cứng, kích thước ≤ slot. Trong lúc nhận, tính SHA-256 dần.
- Kết thúc mà khớp thì ghi bản ghi `PENDING`, trả 200 rồi khởi động lại.
- Body dở hoặc sai SHA thì trả 400, trạng thái boot giữ nguyên.

**Xác nhận và rollback** (giữ hành vi F12 của ESP32, `sysmgr.c:225`):
- Có dịch vụ đã cấu hình mà **khởi tạo** lỗi (bridge, MQTT, Edge, web) thì app ghi `RESTORE(0)` ngay rồi reset.
  - "Khởi tạo" nghĩa là tạo được task, socket listener và tài nguyên.
  - Việc broker hay Internet không truy cập được **không** tính là lỗi khởi tạo.
- Mọi dịch vụ đã khởi tạo xong và chạy được 30 s thì `ota_confirm` ghi `CONFIRMED`.
- **Deadline của `TRIAL`:** `init` chưa xong sau 60 s, hoặc chưa xác nhận sau 120 s kể từ lúc boot, thì `wdog` ngừng kick. Board reset và `trial` tăng lên. App bị treo hay HardFault cũng làm `trial` tăng.

### 11.4 Các cơ chế an toàn khác

- **SGM706:** timeout tối thiểu 1,0 s, danh định 1,6 s (datasheet SGM706, trang 4). `wdog` kick mỗi 200 ms.
  - Vì `wdog` là task chứ không phải ISR, scheduler treo thì watchdog sẽ reset board.
  - `wdog` còn giám sát deadline của `init` và `TRIAL` (§11.3).
  - Ngoài phạm vi này, tiến triển của từng task dịch vụ **không** được giám sát (giữ như ESP32).
  - Phải chốt đúng hậu tố của SGM706 trên BOM, vì mỗi hậu tố có ngưỡng reset khác nhau.
- **HardFault và vi phạm MPU:** handler ghi PC, LR, CFSR, HFSR, MMFAR/BFAR và tên task vào backup SRAM, rồi reset. Lần boot sau, thông tin này được ghi vào log. Đây là phần thay cho phân vùng coredump của bản ESP32.
- **Reload:** nhấn 3–15 s để khôi phục cấu hình gốc, giữ lại log.
- **BOR:** đặt option byte BOR level 2.

## 12. Kiểm thử và công cụ

### 12.1 Host test (`test/host`, ASan/UBSan, `-Werror`, cJSON lấy từ `third_party`)

| Nhóm | Nội dung |
|---|---|
| Chép từ ESP32 | Test của `cfgmodel` và `proto`, bỏ phần RFC 2217 và Telnet |
| `jsonscan` | Chuỗi chứa ngoặc hoặc dấu phẩy, escape, thứ tự key bất kỳ, văn bản cụt, dữ liệu không phải JSON |
| Tương đương Edge | Chạy các payload trong mock của UI và trong bộ test cũ (thu về ≤ 100 điểm), cộng thêm các payload đặc biệt: <br>• gửi ít trường nhưng văn bản chuẩn hóa lớn (sát 48 KB); <br>• nhiều lỗi cùng lúc, kiểm câu báo lỗi **đầu tiên**; <br>• thứ tự thiết bị và node bị đảo; <br>• tên khác nhau nhưng trùng hash; <br>• trùng ID với thiết bị không map, chồng thanh ghi với node không map. <br>Kết quả phải giống từng byte với `cfg_validate` nguyên khối |
| Arena | Chạy đúng bộ TLSF trên vùng 64 KB, giả lập kích thước node cJSON 32-bit, tính cả phân mảnh. Kiểm: <br>• đỉnh của payload lớn nhất của **từng** cấu hình; <br>• đỉnh của mô hình Edge 100 điểm; <br>• arena bị poison ở `arena_end` để bắt truy cập sau khi phạm vi kết thúc (template report, chuỗi); <br>• hết bộ nhớ không bao giờ sinh ra cấu hình thiếu trường |
| Store Edge | `POST` và `GET` chạy xen nhau (generation bị ghim), dọn generation cũ và bản ghi mồ côi sau mất điện ở từng bước commit, FlashDB dọn rác trong lúc `GET` |
| Bootloader | NOR giả lập mô hình được **ghi dở**: một page hay một sector chỉ ghi hoặc xóa được một phần khi bị cắt điện. Cắt điện tại **mọi** thao tác ghi và xóa của: <br>• bản ghi trạng thái; <br>• mọi pha (BACKUP, WRITE, VERIFY, RESTORE); <br>• lúc tăng `trial`, lúc xác nhận. <br>Thêm các trường hợp:
• staging hỏng, backup hỏng, cả hai bản ghi hỏng, flash trắng, `seq` quay vòng;
• image ngắn nhất và dài nhất;
• lỗi flash (deadline), mất điện liên tiếp trong lúc `RESTORE`;
• **RESTORE xong mà SHA sai**;
• **`attempts` vượt 3** ở từng pha;
• **image hỏng khi đang `TRIAL`**: phải không nhảy vào;
• **SPI4 lỗi**: chạy `degraded`;
• cắt điện đúng lúc ghi `attempts`.
 Điều kiện đạt: lần nào cũng kết thúc ở một image hợp lệ, hoặc ở chế độ cứu hộ khi cả hai image thật sự hỏng |
| MQTT | Adapter transport (đọc một phần, AGAIN, EOF, lỗi). Outbox: mất kết nối ở từng bước QoS 1/2, Session Present 0 và 1 (3.1.1), **3.1 với `cleanSession` 0 và 1** (bản vá đặt `sessionPresent`), clean session, hết hạn chỉ áp dụng cho `QUEUED`, giới hạn 64 KB, `bufferOffline`, đăng ký lại subscribe. Byte của gói CONNECT 3.1 và 3.1.1 |
| CMUX | Mã hóa và giải mã frame, FCS, frame bị cắt, trường độ dài 1 và 2 byte (kể cả đúng 2048 byte), tái đồng bộ sau rác, SABM/UA kèm T1 và N2, DM/DISC, CLD. **FCoff/FCon dừng và tiếp toàn bộ mux; MSC dừng và tiếp đúng một DLC**, có trả response, Test và NSC. **Luồng PPP có nhiều `0x7D`/`0x7E`** (sau escape dài gần gấp đôi) bị cắt thành nhiều frame ≤ N1 rồi ghép lại đúng. `pppos_input_tcpip` trả `ERR_MEM`: thử lại 3 lần rồi bỏ và đếm |
| ADC (logic C thuần) | Bàn giao hai nửa buffer: đọc đúng nửa đã đầy; `seq` đổi trong lúc cộng thì bỏ cửa sổ và đếm `adc_late`; chưa có cửa sổ đầu tiên thì không công bố giá trị; lỗi thì cả hai kênh failed; trình tự phục hồi; đổi mã sang mA kèm hiệu chuẩn hai điểm; factory record thiếu hoặc CRC sai thì dùng hệ số danh định |
| Cấu hình nhỏ | Văn bản chuẩn hóa lớn nhất của từng cấu hình (chuỗi dài tối đa, toàn ký tự phải escape) ≤ 10 KB. Vòng `POST → GET → POST` cho ra cùng văn bản |
| Cắt frame RTU | Logic t1.5/t3.5 viết dưới dạng C thuần, đầu vào là chuỗi dấu thời gian của từng byte: khoảng hở < t1.5, trong [t1.5, t3.5), và ≥ t3.5. Baud > 19200 dùng giá trị cố định |

### 12.2 Bench HIL (`test/bench`)

- **Hạ tầng:**
  - board và máy test cùng LAN;
  - **UART1** nối qua adapter USB-RS485, được bọc thành đúng giao tiếp socket mà các test hiện dùng;
  - relay USB cắt và cấp lại nguồn;
  - `bench_env.py` thay cho `qemu_env.py`.
- **Thiết bị đo** cho những mục mà adapter USB không đo chính xác được:
  - logic analyzer hoặc oscilloscope cho DE, t1.5/t3.5 và WDI. WDI đo **ngay trên chân**, cả trong bootloader lẫn trong app;
  - một bộ phát RS485 có kiểm soát timing (một MCU thứ hai hoặc logic analyzer có chức năng phát) để tạo các khoảng hở t1.5–t3.5;
  - nguồn dòng 4–20 mA cho AI1/AI2;
  - fixture DI/DO.
- **Các bộ test port từ `test/qemu`:** bridge, MQTT, Edge, misc, OTA.
  - Bỏ các test của Telnet, RFC 2217 và PUSR.
  - Mô hình Edge ≤ 100 điểm.
  - E2E chỉ chạy trên UART1 (D7).
- **Bổ sung, kiểm trên phần cứng:**
  - timing DE (DE giữ tới hết stop bit cuối), t1.5/t3.5, XON/XOFF, nhận liên tục sau nhiều lần RTO, DMA quay vòng có overrun;
  - **UART2:** đo riêng pinmux, DE và timing một lần. Chạy **hai bus đồng thời**: Edge poll trên UART2 trong lúc bộ test bridge chạy trên UART1;
  - DI, DO qua 74HC573 (kể cả trạng thái relay trong lúc bootloader chạy). AI1 và AI2 kiểm bằng nguồn dòng 4, 12 và 20 mA: hiệu chuẩn, độ ổn định khi có nhiễu 50 Hz bơm vào, khôi phục sau lỗi OVR, và xác nhận ADC không phát sinh ngắt theo từng mẫu;
  - PPP + CMUX trên EC600U:
    - tải HTTPS lớn và liên tục trong lúc DLC2 đọc tín hiệu, `overrun` phải bằng 0;
    - phục hồi DLC;
    - chuyển đổi ưu tiên kết nối, kèm kết nối lại các phiên TCP/TLS;
  - multicast;
  - `wdiMaxGapMs` trong lúc OTA, lúc FlashDB dọn rác và lúc format lần đầu;
  - **OTA bị cắt điện ngẫu nhiên 20 lần.** Đây là smoke test; phần fault injection có chủ đích nằm ở host test của bootloader.
  - **Flood LAN:** bơm broadcast 90 Mbit/s trong lúc UART1 chạy liên tục ở 230400 baud và PPP đang tải. Điều kiện đạt: `overrun` = 0 trên UART1 và UART3, Web UI vẫn trả lời, `wdiMaxGapMs` < 400 ms (§6.1).
  - **ADC:** mọi tiêu chí trong bảng nghiệm thu ở §6.6, kiểm bằng nguồn dòng chuẩn và bộ bơm nhiễu 50/60 Hz.
  - **MQTT QoS:** ngắt kết nối ở từng pha QoS 1/2, với 3.1 và 3.1.1, `cleanSession` 0 và 1 (§8).

### 12.3 Kiểm bộ nhớ

Hai cổng không thể đồng thời vừa là TCP server vừa là TCP/TLS client, nên tải tối đa được tách thành hai kịch bản:

| Kịch bản | Cấu hình |
|---|---|
| A: nhiều kết nối | UART1 và UART2 đều là TCP server, mỗi cổng 16 client. MQTT có TLS. 4 client WebSocket. Edge 100 điểm, 5 thiết bị Modbus TCP, **template report 100 slot** (buffer 26 KB) |
| B: nhiều TLS | UART1 là TCP client có TLS, UART2 là HTTPS client. MQTT có TLS, ba handshake chạy cùng lúc. Outbox đầy (broker tắt, `bufferOffline`). Một HTTP client tải chậm UI. FlashDB đang dọn rác |

Bộ test đọc `/api/status.mem` sau mỗi kịch bản. Điều kiện đạt:
- mọi stack còn dư ≥ 1 KB;
- heap AXI còn trống thấp nhất ≥ 16 KB;
- đỉnh arena ≤ 60 KB;
- không pool tĩnh nào đầy, trừ những pool được thiết kế để bỏ dữ liệu khi đầy (outbox, ring) và có bộ đếm.

Ngoài ra, file map của linker phải cho thấy mỗi section nằm trong ngân sách ở §5.3.

### 12.4 Công cụ, build và nạp

- `embed_web.py`, `check_schema.py`, `audit_keys.py`: bản riêng, trỏ vào `stm32/web` và `stm32/firmware`.
- `mkimage.py`: thêm header.
- CMake preset `boot` và `app`. Kết quả gồm `boot.elf`, `app.elf` và `app.img` (đã có header).
- Nạp lần đầu qua SWD (P6) bằng OpenOCD với flash bank `stmqspi`, QSPI bank 2. Script phải đặt `FSEL`=1 trước khi probe. Flash nội nạp cùng lúc. Đường thay thế là STM32CubeProgrammer, nhưng cần một external loader viết riêng cho board.
- Log debug ra UART7, 115200 baud.

## 13. Khác biệt so với bản ESP32

| Mục | ESP32 (GW100) | STM32 (H750) |
|---|---|---|
| Cổng nối tiếp | 1 × RS485. Schema đã có sẵn `port2`, nhưng bị tắt theo phần cứng (`cfgmodel.c:178`) | UART1 (RS485 và RS232 song song) và UART2 (RS485), bật `port2` |
| AI | 1, khoảng 12 bit hiệu dụng, đọc bằng phần mềm | 2, ADC3 16 bit, VREF 3,0 V. Oversampling ×16 bằng phần cứng, TIM6 kích 1 kHz, BDMA vòng với hai nửa, trung bình 100 ms, hiệu chuẩn hai điểm lấy từ factory record. Tiêu chí định lượng ở §6.6 |
| TLS bridge | Chỉ UART1 | Cả hai cổng |
| Edge | 300 điểm, body 96 KB | **100 điểm, 64 thiết bị, body và văn bản chuẩn hóa ≤ 48 KB** |
| Body của các cấu hình khác | 96 KB | **10 KB**, kể cả văn bản chuẩn hóa |
| `bufferOffline` | 64 KB | **32 KB mỗi cổng** |
| Ring `mqtt_io` | 2 × 16 KB | **2 × 12 KB** |
| Telnet, RFC 2217, PUSR VCOM | Có | **Bỏ** |
| MQTT TLS khi chưa có giờ (với `tlsVerify` ≥ 1) | Chờ (`WAITING FOR CLOCK`) | **Chỉ bỏ qua kiểm tra ngày**, giống bridge |
| Modem | EC800, không có RESET_N. Thử CMUX, không được thì PPP thẳng (`modem.c:122`) | EC600U, có RESET_N, timing của EC600U, lệnh CMUX đầy đủ tham số. Giữ đường PPP thẳng làm dự phòng |
| Cắt frame RTU | Theo khoảng lặng, không đo được t1.5 (F14) | Receiver Timeout cộng timer, **thực thi t1.5/t3.5** khi chạy transaction Modbus |
| OTA, xác nhận | Hai slot ESP-IDF. Mất điện trước khi xác nhận thì rollback ở lần boot đầu (`test_ota.py:65`) | Bootloader riêng có staging và backup. Khởi tạo lỗi thì rollback ngay. Reset hoặc treo trong lúc `TRIAL` được thử lại tối đa 3 lần rồi mới rollback, để chịu được nguồn chập chờn |
| OTA trong lúc chưa xác nhận | Không chặn | **409** |
| Coredump | Phân vùng flash | Bản ghi HardFault trong backup SRAM |
| Web | `esp_http_server` | Mongoose |
| MQTT | esp-mqtt | coreMQTT cộng outbox riêng |

## 14. Rủi ro và các mục cần xác minh

| Rủi ro hoặc mục cần xác minh | Cách xử lý |
|---|---|
| Startup XIP: `SystemInit` và các hàm HAL đụng tới RCC hoặc QSPI | `system_app.c` riêng. Kiểm disassembly, boot nguội và boot ấm (§6.2) |
| Cấu hình QSPI: QE, opcode, dummy cycle, prescaler | Lấy từ datasheet GD25Q32E. Kiểm XIP ở tần số đích khi bring-up |
| Hiệu năng XIP (TLS handshake, Edge 10 ms) | Đưa code nóng vào ITCM. Đo thời gian handshake và độ trễ linkage trên bench |
| Cache và DMA (ETH, USART, SPI4) | Quy tắc §5.4. Test bench có tải lớn |
| Ngân sách RAM | File map của linker và `/api/status.mem` với hai kịch bản (§12.3). Nếu thiếu, giảm `MEM_SIZE` hoặc số client WebSocket trước khi đụng tới tính năng |
| Thời gian EC600U cần để UART sẵn sàng | Đo trên bench, rồi đặt deadline theo quy tắc ở §7.2 (mặc định 15 s) |
| Độ chính xác ADC: settling ở 64,5 cycle, sai số, nhiễu | Bảng nghiệm thu ở §6.6. Không đạt settling thì tăng lên 387,5 cycle |
| CMUX theo phiên bản firmware của EC600U | Ghi `AT+QGMR` vào log. Giữ đường dự phòng PPP thẳng |
| UART3 không có RTS/CTS | DMA 4 KB với HT, TC, IDLE. Bench tải liên tục, `overrun` = 0 |
| t1.5/t3.5 trên phần cứng | Logic analyzer và bộ phát RS485 có kiểm soát timing (§12.2) |
| Thời gian tối đa khi xóa và ghi GD25Q32E | Lấy từ datasheet, dùng làm deadline (§11.2) |
| Hậu tố của SGM706 trên BOM | Chốt cùng nhóm phần cứng |
| Thời gian mở FlashDB qua SPI | Đo khi bring-up. Nếu chậm thì thêm cache header trong RAM |
| OpenOCD `stmqspi` trên bank 2 | Script đặt `FSEL`=1 trước khi probe. Kiểm ngay ở bước bring-up đầu tiên |
| Cửa sổ relay lúc cấp nguồn (R103 kéo OE xuống) | Ghi nhận là giới hạn của board hiện tại (§11.2). Đề xuất đổi phần cứng |
| License Mongoose | Quyết định GPLv2 hay thương mại trước khi phát hành |

## 15. Ngoài phạm vi

- SNMP, Cloud (AWS, Azure, PUSR…), USB (cả MCU và đường USB tới EC600U), Telnet, RFC 2217, PUSR/USR-VCOM.
- Socket B, máy in serial, IO mở rộng, PID, UART8.
- Ký image và secure boot. F01 vẫn được chấp nhận như bản ESP32.
- Nguồn giờ ngoài NTP và đặt tay.
- Thay đổi firmware và UI của ESP32.
