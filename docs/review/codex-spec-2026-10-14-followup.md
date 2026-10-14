# Xử lý review spec của Codex (gpt-6-astra, high), 2026-10-14

**Review:** `codex-gpt-6-astra-spec-2026-10-14.md`. **Prompt:** `codex-prompt-spec-2026-10-14.md`.
**Spec được sửa:** `stm32/docs/specs/2026-10-14-stm32-firmware-design.md`.

Mọi phát hiện đều được kiểm lại trước khi sửa. Những phát hiện dưới đây đã được xác nhận bằng mã nguồn hoặc phép đo:

- **#6:** `edgecore.c:358`, `t->text` và `frag[]` trỏ vào văn bản do cJSON cấp phát.
- **#7:** đo bằng `cfgmodel` thật, 100 điểm. Body gửi lên 11–16 KB, nhưng văn bản chuẩn hóa lên 26–36 KB tùy số thiết bị (1, 10, 64).
- **#11:** `tlsconn.c:158`, `TLS_AGAIN` = −2, và 0 nghĩa là kết nối đã đóng.
- **#13:** thứ tự 3 pha của `slaves()` và `slaves_strict()`.
- **#18:** `api.c` gọi `free()` trên kết quả của `store_get_json`.

**Không phát hiện nào bị bác bỏ.** Các quyết định D1–D12 giữ nguyên.

| # | Mức | Phát hiện | Cách xử lý trong spec |
|---|---|---|---|
| 1 | Nghiêm trọng | `SystemInit()` chạy trước `main()` và reset RCC | §6.2: `system_app.c` riêng, trình tự của `Reset_Handler`, trạng thái bàn giao, kiểm disassembly, boot nguội và boot ấm |
| 2 | Cao | PWRKEY 700 ms là timing của EC800 | §7.2: bảng timing của EC600U, PWRKEY ≥ 2 s (mặc định 2,5 s), `AT+QPOWD`, RESET_N |
| 3 | Cao | Slot không căn 64 KB | §5.2: layout mới, mọi vùng căn 64 KB, slot 1.472 KB. Checkpoint 64 KB tách khỏi xóa 4 KB và ghi page |
| 4 | Cao | State machine OTA chưa đủ | §11.2: bản ghi A/B (ghi CRC sau cùng, `seq` quay vòng), bảng trạng thái, nguồn không đổi, kiểm nguồn trước khi xóa đích, các trường hợp biên. §11.1: phạm vi SHA, kiểm MSP và reset vector |
| 5 | Cao | Không xác nhận không tự gây rollback | §11.3: khởi tạo lỗi thì `RESTORE` ngay; deadline `init` 60 s, xác nhận 120 s |
| 6 | Cao | Template report trỏ vào arena | §9.3: dữ liệu sống lâu phải chép ra; template biên dịch bằng heap; arena bị poison trong host test |
| 7 | Cao | Văn bản chuẩn hóa lớn hơn input | §9.4: giới hạn 48 KB cho cả body lẫn văn bản chuẩn hóa, kiểm khi ghi |
| 8 | Cao | Arena tuyến tính không thu hồi | §9.3: chuyển sang TLSF có thu hồi, mỗi node một phạm vi con |
| 9 | Cao | Nhiều request trong cùng event loop | §9.3: arena chỉ dùng đồng bộ trong một callback; buffer văn bản Edge riêng có một chủ sở hữu, trả 503 khi bận; 411 khi thiếu `Content-Length` |
| 10 | Cao | RTO ở t3.5 không thực thi t1.5; HAL hủy DMA khi RTOF | §6.5: hai cách cắt frame, RTO ở t1.5 cộng timer, giá trị cố định khi baud > 19200, ISR viết bằng LL |
| 11 | Cao | Hợp đồng transport và outbox của coreMQTT | §8: bảng đổi mã trả về; bảng trạng thái QoS 1/2; chỉ gửi lại khi kết nối lại; hết hạn chỉ cho `QUEUED` |
| 12 | Cao | PPP liên tục không có RTS/CTS; CMUX chưa đủ | §7.2: DMA 4 KB với HT, TC, IDLE, bộ đếm overrun; state machine CMUX; cách thoát data mode bằng `+++`; bench tải liên tục |
| 13 | TB | Tương đương Edge | §9.4: 3 pha đúng thứ tự `cfgmodel`, giới hạn 64 thiết bị và 5 thiết bị Modbus TCP, so khớp với node và thiết bị không map, `strcmp` sau hash. §12.1: test vi phân |
| 14 | TB | Dọn generation, GET đọc nhất quán | §9.4: thứ tự commit, dọn bản ghi mồ côi lúc boot, GET ghim generation |
| 15 | TB | Ngân sách RAM và điều kiện đạt | §5.3: dùng file map của linker, chỉ AXI có heap, đo pool tĩnh. Stack `wdog` 2 KB, ring `mqtt_io` 2 × 12 KB. §12.3: hai kịch bản tải |
| 16 | TB | Cấu hình lwIP chưa đủ | §5.6: bảng pool là việc đầu tiên của `netmgr`; ENOMEM/ENOBUFS là lỗi tạm thời; kiểm soát nhận kết nối |
| 17 | TB | Khẳng định về ưu tiên; StreamBuffer nhiều task ghi | §6.1: ghi rõ điều được bảo đảm và không. §6.4: MessageBuffer mỗi cái một task ghi, thứ tự khóa, không giữ mutex khi gọi transport |
| 18 | TB | `realloc`; quyền sở hữu kết quả `store_get_json` | §5.5: wrapper có header kích thước; `store_get_json` vẫn trả bản sao do bên gọi sở hữu |
| 19 | TB | Watchdog bị "nuôi" khi thao tác bị kẹt | §11.2: deadline cho mọi thao tác flash, quá deadline thì ngừng kick. §11.4: SGM706 tối thiểu 1,0 s, giám sát `init` và `TRIAL`, hậu tố trên BOM |
| 20 | TB | Cấu hình QSPI; 74HC573 | §6.2: QE, `0xEB`, dummy cycle, reset flash. §11.2: OE = 1 trước, ghi cửa sổ không bảo đảm |
| 21 | Thấp | Bảng khác biệt chưa đủ | §13: giới hạn body, cách xác nhận OTA, CMUX có từ ESP32, `port2` đã có trong schema, điều kiện `tlsVerify` ≥ 1, ghi bộ đếm boot (§9.1) |
| — | Ghi chú | DMA: căn lề, barrier, BDMA; ADC; mbedTLS; route; UART3 | §5.4, §6.6 (ADC3: ban đầu là polling, đã thay bằng oversampling + BDMA theo yêu cầu của chủ dự án), §7.3 (mbedTLS 3.6, cipher), §7.2 (route), §6.3 (UART3 thuộc `lte`) |

## Vòng 2 (`codex-gpt-6-astra-spec-r2-2026-10-14.md`): NOT APPROVED

Kết quả vòng 2: 13 phát hiện của vòng 1 đã đạt, 8 phát hiện xử lý chưa đủ (#2, 4, 11, 12, 15, 16, 17, 19), cộng thêm R2-01…10. Tất cả đều được chấp nhận và sửa như dưới đây.

Một chỗ phải cải chính: spec từng ghi "ADC3 chỉ có BDMA". Câu này sai, vì ADC3 còn có request 115 trên DMAMUX1. Spec giờ ghi việc dùng BDMA là **lựa chọn thiết kế**.

Còn mục 16 (bảng pool lwIP): mức "Xử lý chưa đủ" được giữ. Bảng pool vẫn là việc đầu tiên của `netmgr` trong kế hoạch. Spec chỉ ghi ngân sách vùng SRAM1, và quy định rằng bảng pool phải nằm vừa trong ngân sách đó.

| Mục | Cách xử lý |
|---|---|
| R2-01 (#4, #19) OTA | §11.2: kiểm image trước **mọi** lần nhảy, kể cả `TRIAL`. `attempts` được ghi trước mỗi checkpoint. Thêm nhánh RESTORE xong mà SHA sai (`verify_fail`). SPI4 lỗi thì chạy `degraded` và từ chối OTA. §12.1 có fault injection tương ứng |
| R2-02 (#11) MQTT 3.1 | §8: với 3.1, phiên được giữ khi và chỉ khi `cleanSession` = 0, bản vá đặt `sessionPresent` theo quy tắc này. Phân công: coreMQTT giữ trạng thái QoS và gửi lại PUBREL, outbox giữ payload và gửi lại PUBLISH (DUP). Luôn đăng ký lại subscribe. Bench ngắt kết nối ở từng pha QoS |
| R2-03 ADC | §6.6: thanh ghi `TROVS` = 0, `OVSR` = 15, `OVSS` = 4, `DMNGT` = 11, TIM6 (`EXTSEL` = 13), clock 10 MHz với `BOOST` = 01. Hai nửa buffer 800 B với HT/TC, kèm `seq` để phát hiện đọc trễ. Lỗi thì cả hai kênh failed. Trình tự phục hồi có thứ tự. Chỉ công bố giá trị sau khi có đủ 100 cặp mẫu mới. API hiệu chuẩn đầy đủ. Region MPU nằm trong `Reset_Handler` |
| R2-04 (#12) CMUX/PPP | §7.2: FCon/FCoff áp cho toàn mux, MSC áp cho từng DLC, có response, Test và NSC. PPP là luồng byte, cắt thành frame ≤ N1. `ERR_MEM` thì thử lại 3 lần rồi bỏ và đếm. Hàng đợi TX 4 KB khi FCoff. Overhead frame là 7 B |
| R2-05 Cấu hình nhỏ | §9.3, §10: body và văn bản chuẩn hóa ≤ 10 KB, có test vòng `POST → GET → POST` với chuỗi toàn ký tự phải escape |
| R2-06 (#15) Ngân sách | §5.3: buffer report ≤ 26 KB, buffer mạng coreMQTT 2 × 4 KB, AXI còn dư khoảng 45 KB. §9.4: giới hạn template theo số slot, trạng thái `TEMPLATE TOO LARGE`, kiểm lúc lưu. §12.3: kịch bản có template 100 slot |
| R2-07 (#17) Ưu tiên | §6.1: `serial_rx[n]` (26) và `cmux_rx` mới (25) có ưu tiên trên `tcpip`; `eth_rx` block thật bằng `vTaskDelay(1)`. Tiêu chí flood 90 Mbit/s với `overrun` = 0 |
| R2-08 (#2) EC600U | §7.2: bảng timing có giá trị cụ thể (PWRKEY ≥ 2 s → dùng 2,5 s; tắt nguồn ≥ 3 s → 3,5 s; RESET_N ≥ 100 ms → 200 ms; deadline chờ UART 15 s, chỉnh lại theo số đo). Ghi rõ nguồn là bản QuecOpen V1.3 |
| R2-09 Nghiệm thu ADC | §6.6: bảng tiêu chí định lượng gồm sai số, độ dao động, nhiễu 50/60 Hz, ảnh hưởng chéo, độ trễ, phục hồi, số ngắt. Hiệu chuẩn hai điểm lưu trong factory record ở SPI4 0x3F0000 |
| R2-10 Nhất quán | §5.4 (ADC ở SRAM4, MDMA), §6.2/6.3 (MPU, cache, ITCM chỉ làm trong `Reset_Handler`), §5.5 (template cấp từ heap), §10.1 (một quy tắc timeout chung 2 × 15 s), §5.2/§11.2 (cách làm tròn khi xóa), §7.2 (overhead 7 B), §10.2 (`port2` được bật), §5.3 (DTCM khoảng 95 KB, ≤ 120 KB) |

## Vòng 3–7: từ điều kiện tới phê duyệt

| Vòng | Quyết định | Nội dung chính |
|---|---|---|
| 3 | APPROVED WITH CONDITIONS | S1–S7 phải sửa trong spec, P1–P3 để cho kế hoạch |
| 4 | APPROVED WITH CONDITIONS | S1 (các nhánh OTA), S6 (trạng thái tắt của modem), câu ở S7, diễn giải PPP |
| 5 | APPROVED WITH CONDITIONS | Bảng quyết định OTA: hàng 8 che hàng 9; đường RAM chưa có nhánh thất bại |
| 6 | APPROVED WITH CONDITIONS | Đường RAM giả định sai rằng `Vq` = false |
| 7 | **APPROVED** (commit `8a52dbd`) | Kế hoạch triển khai bắt buộc phải bao gồm: <br>• P1: bảng pool lwIP, file map của linker, đo trong hai kịch bản ở §12.3; <br>• P2: giới hạn công việc mỗi lượt, backpressure, test flood và starvation, bàn giao buffer ADC, nghiệm thu cả cấu hình dự phòng; <br>• P3: factory record, loader SPI4, vòng đời của handoff block; <br>• fault injection OTA cho mọi hàng của bảng quyết định; <br>• modem ON/OFF/UNKNOWN và xung PWRKEY 2,5 s; <br>• CMUX flow control và PPP bị cụt giữa các lần gọi callback; <br>• MQTT reconnect/QoS; <br>• JSON round-trip và lỗi 413 |
