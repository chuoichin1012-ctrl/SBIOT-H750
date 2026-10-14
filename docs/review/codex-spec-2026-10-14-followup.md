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
| — | Ghi chú | DMA: căn lề, barrier, BDMA; ADC; mbedTLS; route; UART3 | §5.4, §3 (ADC3 polling), §7.3 (mbedTLS 3.6, cipher), §7.2 (route), §6.3 (UART3 thuộc `lte`) |
