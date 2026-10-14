**Spec chưa đủ để chốt kế hoạch triển khai toàn bộ firmware.** Kiến trúc tổng thể khả thi, nhưng còn lỗi cụ thể ở khởi động XIP, timing EC600U, vòng đời bộ nhớ và Modbus; cơ chế OTA chưa được đặc tả đủ để bảo đảm phục hồi sau mất điện.

Tôi chỉ đọc, không sửa, tạo hay xóa tệp. Đây là review tĩnh và đối chiếu tài liệu; chưa có kết quả build hoặc HIL. Các rủi ro phụ thuộc cách triển khai được ghi rõ là **suy luận**. Tôi giữ nguyên D1–D12 và chỉ đề nghị sửa phần hiện thực hóa chúng.

Đối chiếu phần cứng cho kết quả sau:

| Vùng RAM | Địa chỉ đầu | Dung lượng | ETH DMA | DMA1/2 | BDMA |
|---|---:|---:|---|---|---|
| ITCM | `0x00000000` | 64 KiB | Không | Không | Không |
| DTCM | `0x20000000` | 128 KiB | Không | Không | Không |
| AXI SRAM | `0x24000000` | 512 KiB | Có | Có | Không |
| SRAM1 | `0x30000000` | 128 KiB | Có | Có | Không |
| SRAM2 | `0x30020000` | 128 KiB | Có | Có | Không |
| SRAM3 | `0x30040000` | 32 KiB | Có | Có | Không |
| SRAM4 | `0x38000000` | 64 KiB | Không | Có | Có |

Bảng §5.3 đúng về dung lượng. Cần bổ sung rằng ETH không truy cập SRAM4, còn BDMA không truy cập SRAM1; quy tắc “mọi buffer DMA ở SRAM1” chỉ đúng với các engine đã chọn, không đúng cho BDMA. MDMA có thể truy cập TCM. Nguồn: [ST AN4891, bảng 4 và 6](https://www.st.com/resource/en/application_note/dm00306681-stm32h74x-and-stm32h75x-system-architecture-and-performance-expansion-package-for-stm32cube-stmicroelectronics.pdf), [hướng dẫn DMA của ST](https://community.st.com/stm32-mcus-60/dma-is-not-working-on-stm32h7-devices-97).

| Hạng mục | Kết quả đối chiếu |
|---|---|
| Flash nội | 128 KiB; sector 128 KiB. §5.1 đúng. |
| QUADSPI | PB2/PC11, PE7–PE10 đúng đường bank 2. |
| Hardware DE | PA12/PD4 hỗ trợ USART1/2 DE qua AF7. |
| ADC | **PC2_C = ADC3_INP0; PC3_C = ADC3_INP1** khi đo single-ended. |
| DI | Schematic trang 10 xác nhận pull-up 3,3 V, opto kéo xuống: active-low đúng. |
| DO | Trang 11 xác nhận D1/D2, LE/OE, transistor và relay như spec. |
| LTE | Trang 6 xác nhận dịch mức 1,8 V, không nối RTS/CTS, GPIO cao kích transistor PWRKEY/RESET_N. |

Nguồn pin và flash: [STM32H750 DS12556 Rev 8](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf); nguồn mạch: [Job2.PDF](/home/linh/projects/SBIOT-Gateway/docs/Job2.PDF), trang 2, 4–11.

Các phát hiện dưới đây được xếp theo mức độ.

1. **Nghiêm trọng — §6.2–6.3: bỏ hai lời gọi trong `main()` chưa bảo vệ được XIP.**

   Startup GCC chính thức gọi `SystemInit()` **trước `main()`**. `SystemInit()` mặc định reset RCC, tắt PLL và sửa prescaler. Vì vậy, chỉ bỏ `SystemClock_Config()` và `MX_QUADSPI_Init()` như spec vẫn để đường khởi động phá cấu hình mà bootloader bàn giao.

   **Rủi ro:** app có thể treo trước `board_early()`; `SystemCoreClockUpdate()` không khắc phục được việc này.

   **Sửa spec:** yêu cầu startup/SystemInit riêng cho app XIP, không reset clock/QSPI/pinmux; xác định thứ tự bật SRAM, khởi tạo section, chép ITCM, thiết lập VTOR/cache và bàn giao interrupt. Kiểm bằng disassembly và cold boot. Nguồn: [startup STM32H750 của ST](https://raw.githubusercontent.com/STMicroelectronics/cmsis-device-h7/master/Source/Templates/gcc/startup_stm32h750xx.s), [SystemInit của ST](https://raw.githubusercontent.com/STMicroelectronics/cmsis-device-h7/master/Source/Templates/system_stm32h7xx.c).

2. **Cao — §7.2: xung PWRKEY 700 ms không đúng EC600U.**

   Tài liệu EC600U yêu cầu kéo PWRKEY thấp **ít nhất 2 s** để bật nguồn. Giá trị 700 ms đến từ EC800 trong [modem.c:47](/home/linh/projects/SBIOT-Gateway/firmware/components/netmgr/modem.c:47).

   **Rủi ro:** modem đang tắt không khởi động; kích RESET_N tiếp theo không thay thế thao tác bật nguồn.

   **Sửa spec:** dùng timing EC600U, quy định thời gian chờ UART sẵn sàng sau bật/reset và độ dài RESET_N. Không suy ra “modem hỏng” ngay sau xung nguồn. Nguồn: [Quectel EC600U Hardware Design V1.3, §3.7](https://fccid.io/XMR2024EC600ULA/User-Manual/User-Manual-7494081.pdf), được đối chiếu thêm với [tài liệu trên website Quectel, §3.8](https://developer.quectel.com/wp-content/uploads/2024/09/Quectel_EC600U%E7%B3%BB%E5%88%97_QuecOpen_%E7%A1%AC%E4%BB%B6%E8%AE%BE%E8%AE%A1%E6%89%8B%E5%86%8C_V1.3.pdf).

3. **Cao — §5.2, §11.2: đơn vị cài đặt 64 KiB không khớp căn chỉnh slot.**

   Backup bắt đầu tại `0x284000`, không thẳng hàng 64 KiB. Lệnh xóa block 64 KiB tại địa chỉ này xóa block bắt đầu `0x280000`, lấn **16 KiB cuối staging**. Hai slot dài `0x17C000`, cũng không chia hết cho 64 KiB.

   **Rủi ro — suy luận theo cách hiện thực “đơn vị 64 KB gồm xóa”:** phá nguồn staging khi backup; xử lý đơn vị cuối không đúng còn có thể vượt cuối flash.

   **Sửa spec:** phân biệt *đơn vị checkpoint* và *đơn vị erase vật lý*. Giữ layout hiện tại thì xóa sector 4 KiB ở các đoạn không căn chỉnh, giới hạn chính xác đoạn cuối và program theo page. Nguồn: [GD25Q32E, §7.17](https://www.mouser.com/datasheet/2/870/gd25q32e_rev1_3_20221013-1825518.pdf). Đây là datasheet hãng Rev 1.3 do Mouser lưu; hãng hiện liệt kê Rev 1.5.

4. **Cao — §11.1–11.2: trạng thái OTA chưa đủ để chứng minh khả năng phục hồi.**

   Hai record luân phiên có sequence/CRC là hướng đúng, nhưng spec chưa quy định thứ tự erase–program–commit, xử lý record ghi dở, và điều kiện chuyển pha. “Rollback bằng cơ chế ở bước 4” còn mơ hồ vì bước 4 chứa cả `BACKUP`: không được chép image TRIAL/hỏng đè lên nguồn rollback.

   **Rủi ro:** chọn sai nguồn sau reset, đánh dấu tiến độ trước khi dữ liệu bền vững, hoặc mất backup tốt.

   **Sửa spec:** thêm bảng chuyển trạng thái và invariant:
   - Giữ record cũ hợp lệ đến khi record mới hoàn tất.
   - Chỉ chuyển `BACKUP → WRITE` sau khi backup đã được kiểm đầy đủ.
   - Có `RESTORING(source, offset)` riêng; nguồn restore bất biến.
   - Kiểm header, kích thước và SHA của nguồn **trước khi xóa đích**.
   - Quy định khi backup hỏng, cả hai record hỏng, flash trắng, sequence quay vòng và VERIFY thất bại.
   - Xác định vùng byte được SHA/CRC bao phủ; kiểm MSP, reset vector và giới hạn image trước khi nhảy.

   Không cần thay D9; cần làm rõ thuật toán D9.

5. **Cao — §11.3: không xác nhận TRIAL không tự gây rollback.**

   Khi một dịch vụ khởi động lỗi, spec chỉ nói không ghi `CONFIRMED`. Nhưng `wdog` vẫn chạy độc lập; app có thể ở TRIAL vô thời hạn và không quay lại bootloader.

   ESP32 hiện gọi rollback-and-reboot ngay khi startup không healthy: [sysmgr.c:225](/home/linh/projects/SBIOT-Gateway/firmware/components/sysmgr/sysmgr.c:225).

   **Rủi ro:** thiết bị chạy thiếu dịch vụ, đồng thời mọi OTA tiếp theo bị 409.

   **Sửa spec:** startup lỗi phải ghi yêu cầu rollback rồi reset; có deadline cho TRIAL/init bị kẹt. Phân biệt “dịch vụ khởi tạo thành công” với “broker/Internet đang truy cập được”. Chặn OTA trong TRIAL là đúng và nên giữ.

6. **Cao — §4, §5.5, §9.4: template report giữ con trỏ vào arena đã được reset.**

   `proto` được chép nguyên; `edge_tmpl_compile()` lưu kết quả `cJSON_PrintUnformatted()` vào `t->text`, rồi `t->frag[]` trỏ vào đó. Spec lại yêu cầu biên dịch template trong arena và giải phóng arena sau boot.

   **Rủi ro:** request web tiếp theo ghi đè nội dung template; report sai hoặc truy cập bộ nhớ không còn thuộc đối tượng.

   **Sửa spec:** mọi dữ liệu sống lâu hơn arena phải được sao chép sang vùng tồn tại lâu dài trước `arena_end()`. Rà cả chuỗi, template và con trỏ cấu hình, không chỉ cây cJSON. Nguồn: [edgecore.c:358](/home/linh/projects/SBIOT-Gateway/firmware/components/proto/edgecore.c:358), [edge_rt.c:873](/home/linh/projects/SBIOT-Gateway/firmware/components/services/edge_rt.c:873).

7. **Cao — §9.3–9.4: giới hạn input 32 KiB không bảo đảm dữ liệu lưu/đọc lại ≤32 KiB.**

   `cfg_validate` bổ sung defaults cho từng device/node. Tôi tính trực tiếp từ `ITEM_SLAVE` và `ITEM_NODE`: một payload gồm 64 thiết bị, 100 điểm, tên 16 ký tự và formula 64 ký tự có:
   - Input compact: **12.933 byte**.
   - Sau bổ sung defaults: **36.553 byte**.

   Đây là phép tính theo schema, **không phải kết quả chạy validator binary**. Các trường và quy tắc liên quan nằm tại [cfgmodel.c:352](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:352), [cfgmodel.c:478](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:478).

   **Rủi ro:** POST hợp lệ, lưu thành công, nhưng boot không đọc được theo giới hạn §9.4; GET trả cấu hình mà POST lại từ chối.

   **Sửa spec:** kiểm riêng kích thước chuẩn hóa trước commit, hoặc đọc/validate từ flash theo luồng. Đồng bộ giới hạn UI, API và boot.

8. **Cao — §5.5, §9.3: arena tuyến tính không tương đương bộ đo “peak live allocations”.**

   `json_free()` không thu hồi trong arena. Validator hiện tạo defaults, duplicate rồi replace các field; cJSON print cũng có cấp phát trung gian. Theo node chỉ giảm RAM nếu có checkpoint/reset giữa các node.

   **Rủi ro — suy luận:** các node đã “free” vẫn tiêu thụ arena, vượt pool 24 KiB; số đo với malloc/free thông thường đánh giá thấp nhu cầu.

   **Sửa spec:** định nghĩa phạm vi cấp phát lồng nhau, checkpoint/rewind từng node, vùng summary tồn tại xuyên lượt và buffer output 8 KiB. Test phải dùng đúng allocator tuyến tính, tính cả alignment và cấp phát đã bỏ nhưng chưa rewind; kiểm OOM không tạo cấu hình chuẩn hóa thiếu trường. Nguồn: [cfgmodel.c:294](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:294).

9. **Cao — §5.5, §9.4, §10.1: task-local ownership chưa xử lý nhiều request trong một event loop.**

   Request A giữ arena trong lúc chờ body; Mongoose vẫn xử lý request B trong cùng task `web`. Task-local pointer không phân biệt A/B.

   **Rủi ro — suy luận:** B dùng chung/reset arena của A; hoặc B chờ mutex do A giữ, làm event loop không thể tiếp tục A.

   **Sửa spec:** arena phải có owner theo connection/request, chỉ gắn allocator trong callback tương ứng; cấp quyền không chặn event loop và quy định phản hồi khi bận. Thêm đường giải phóng khi close/timeout. Giới hạn body phải áp dụng lên số byte thực nhận; quy định xử lý thiếu `Content-Length`/chunked. Nguồn mô hình sự kiện: [Mongoose documentation](https://mongoose.ws/documentation/).

10. **Cao — §6.5, §13: Receiver Timeout tại t3.5 chưa thực thi t1.5.**

    Khoảng im lặng 2 ký tự nằm giữa frame sẽ không kích timeout đặt 3,5 ký tự; frame vẫn bị nối lại. Vì vậy tuyên bố “F14 không còn” chưa đúng. Ở baud trên 19.200, còn thiếu lựa chọn timing cố định 750 µs/1,750 ms.

    Thêm một điểm hiện thực cụ thể: HAL H7 mặc định xử lý `RTOF` như lỗi và abort DMA RX; không thể mặc nhiên dùng handler đó cho circular DMA liên tục.

    **Rủi ro:** nhận frame RTU không hợp lệ hoặc ngừng nhận sau timeout đầu tiên.

    **Sửa spec:** mô tả state machine t1.5/t3.5 riêng cho RTU, chuyển đổi ms/bit-time theo framing, xử lý RTO bằng LL/ISR phù hợp, và test nhận liên tục sau timeout. Giữ quy tắc `packetLength` cho transparent serial. Nguồn: [Modbus Serial V1.02 §2.5.1.1](https://www.modbus.org/file/secure/modbusoverserial.pdf), [HAL UART H7](https://raw.githubusercontent.com/STMicroelectronics/stm32h7xx-hal-driver/master/Src/stm32h7xx_hal_uart.c), [serial.c:118](/home/linh/projects/SBIOT-Gateway/firmware/components/serial/serial.c:118).

11. **Cao — §8: outbox và adapter coreMQTT chưa có hợp đồng đủ chặt.**

    Có hai vấn đề:
    - `tls_read()` trả `TLS_AGAIN=-2`, `0=closed`; coreMQTT yêu cầu `0=chưa có dữ liệu`, số âm=lỗi/disconnect. Đặt trực tiếp lên `tlsconn` sẽ hiểu sai trạng thái.
    - “Gửi lại theo packet id” chưa phân biệt chờ PUBACK, PUBREC, PUBCOMP; chưa quy định tương tác giữa hết hạn 1 giờ và session state.

    **Rủi ro:** reconnect liên tục, lặp PUBLISH sai giai đoạn QoS 2, giải phóng/reuse packet ID khi giao dịch chưa xong.

    **Sửa spec:** adapter chuyển mã rõ ràng; bảng trạng thái outbox liên kết với state của coreMQTT, DUP, Session Present, CleanSession và resubscribe. Tách expiry tin chưa gửi khỏi trạng thái giao dịch đang chạy. `retryInterval` hiện là thời gian reconnect, không mặc nhiên là chu kỳ retransmit.

    Nguồn: [tlsconn.c:158](/home/linh/projects/SBIOT-Gateway/firmware/components/services/tlsconn.c:158), [mqttgw.c:357](/home/linh/projects/SBIOT-Gateway/firmware/components/services/mqttgw.c:357), [transport coreMQTT 2.3.1](https://raw.githubusercontent.com/FreeRTOS/coreMQTT/v2.3.1/source/interface/transport_interface.h), [MQTT 3.1.1 §4.3–4.4](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html).

12. **Cao — §7.2: DMA vòng + IDLE chưa đủ cho PPP liên tục không có RTS/CTS.**

    Luồng liên tục có thể không tạo IDLE trước khi ring quay vòng. Spec chưa nêu HT/TC, kích thước ring, phát hiện overwrite, giới hạn thời gian phục vụ và phản ứng khi `pppos_input_tcpip` không nhận thêm dữ liệu.

    CMUX còn thiếu control DLC0, mở/đóng DLC, timeout/retry, flow control và tái đồng bộ sau reset. Chỉ test encode/decode/FCS chưa kiểm được các hành vi này.

    **Rủi ro — suy luận:** mất byte PPP hoặc kẹt đường AT dưới tải.

    **Sửa spec:** thêm HT/TC cùng IDLE, budget độ trễ và counter overrun; state machine CMUX theo [3GPP TS 27.010 §5](https://www.etsi.org/deliver/etsi_ts/127000_127099/127010/19.00.00_60/ts_127010v190000p.pdf). Đường PPP thẳng phải có cách thoát data mode hoặc reset trước khi gửi AT phục hồi. Không có RTS/CTS không làm D10 bất khả thi, nhưng cần chứng minh bằng tải liên tục.

13. **Trung bình — §9.4: pipeline Edge chưa đặc tả đầy đủ tính tương đương.**

    Bảng summary thiếu hoặc chưa rõ:
    - Tối đa **64 device**, **5 Modbus TCP device**.
    - Mapped/virtual unit phải so với cả unit đang được trả lời bởi thiết bị **không map**.
    - Node mapped phải kiểm overlap với cả node không map trong cùng device/function space.
    - Tên device và tên point là hai phạm vi kiểm trùng khác nhau.
    - Hash trùng phải so lại tên thật; hash không chứng minh hai tên giống nhau.
    - `cfg_load` bỏ strict save-time rules, không giống `cfg_validate`.

    Ngoài ra, validate F15 ngay từng node có thể trả lỗi trước một lỗi cấu trúc ở device sau, khác thứ tự validator hiện tại.

    **Rủi ro:** chấp nhận cấu hình xung đột hoặc từ chối cấu hình vốn hợp lệ; test “giống từng byte” không đạt.

    **Sửa spec:** liệt kê quy tắc, phạm vi và thứ tự lỗi theo [cfgmodel.c:391](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:391), [cfgmodel.c:478](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:478), [cfgmodel.c:623](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:623). Bổ sung differential test với payload có nhiều lỗi, đảo thứ tự device/node và va chạm hash.

14. **Trung bình — §9.1–9.4: đổi generation đúng hướng, nhưng thiếu thu hồi và snapshot đọc.**

    Ghi các chunk trước, đổi manifest cuối phù hợp cơ chế KV của FlashDB. Tuy nhiên spec dùng `gen+1` mà không nói xóa generation cũ/orphan. Các key cũ vẫn là KV hợp lệ nên GC không tự hiểu chúng đã hết giá trị.

    GET gửi từng chunk qua nhiều vòng poll cũng cần giữ một generation cố định; nếu POST đổi manifest rồi xóa chunk cũ giữa chừng, GET có thể thiếu dữ liệu.

    **Rủi ro:** đầy partition sau nhiều lần lưu hoặc response ghép không nhất quán.

    **Sửa spec:** định nghĩa commit, snapshot reader, cleanup sau commit và cleanup orphan sau mất điện; test khi GC chạy. Bản cũ đã có xóa chunk cũ tại [store.c:141](/home/linh/projects/SBIOT-Gateway/firmware/components/store/store.c:141); cơ chế ghi KV tại [fdb_kvdb.c:1295](/home/linh/projects/SBIOT-Gateway/firmware/components/flashdb/FlashDB/src/fdb_kvdb.c:1295). Một lần ghi manifest không có nghĩa toàn giao dịch nhiều key là atomic.

15. **Trung bình — §5.3, §6.1, §12.3: ngân sách RAM và điều kiện PASS mâu thuẫn.**

    | Vùng | Nhận xét từ các số trong spec |
    |---|---|
    | DTCM | Khoảng 84 KiB task stack + MSP 4 KiB còn chỗ; chưa tính đầy đủ TCB, `init`, timer task và static data. |
    | AXI | Các khoản liệt kê tổng khoảng 403 KiB; phần dư khoảng 109 KiB là đúng về số học, chưa phải bằng chứng tải tối đa. |
    | SRAM1 | Chưa có số lượng/kích thước descriptor, RX/TX pool, DMA rings và các pool lwIP để chốt. |
    | SRAM2+3 | 144/160 KiB đã dành cho buffer, chỉ còn đúng 16 KiB trước metadata/alignment. |
    | SRAM4 | 32 + 2×16 = **64 KiB**, không còn dư. |

    `heap_5` chỉ có vùng AXI, nhưng API/test lại đòi heap theo D2/SRAM4. Task `wdog` có stack 1 KiB nhưng mọi task phải còn trống ≥1 KiB: không thể đạt sau khi task chạy.

    **Sửa spec:** phân biệt static reserve, heap, pool occupancy và high-water mark; bổ sung linker-map budget, stack `init`/timer, coreMQTT network buffer và report buffer. Report buffer hiện phụ thuộc số slot template tại [edgecore.c:407](/home/linh/projects/SBIOT-Gateway/firmware/components/proto/edgecore.c:407), không chỉ số point.

16. **Trung bình — §5.6: 64 netconn và `MEM_SIZE=64 KiB` chưa tạo thành cấu hình lwIP hoàn chỉnh.**

    Số netconn có vẻ đủ theo cách đếm hiện tại, nhưng còn PCB/listener, TCP segments, pbuf, netbuf, mailbox, DNS/PPP và socket descriptor limits. Các pool không mặc nhiên nằm trong `MEM_SIZE`.

    Câu “hết bộ nhớ thì socket trả EAGAIN” cũng không đúng tổng quát: lwIP ánh xạ `ERR_MEM→ENOMEM`, `ERR_BUF→ENOBUFS`.

    **Rủi ro:** tạo socket thất bại sớm hoặc xử lý áp lực bộ nhớ như lỗi kết nối vĩnh viễn.

    **Sửa spec:** chốt bảng pool và vùng đặt từng pool, admission control/backpressure, xử lý thiếu bộ nhớ theo từng API; đo đồng thời queue/pool occupancy. Nguồn: [lwIP options](https://raw.githubusercontent.com/lwip-tcpip/lwip/master/src/include/lwip/opt.h), [lwIP errno mapping](https://raw.githubusercontent.com/lwip-tcpip/lwip/master/src/api/err.c).

17. **Trung bình — §6.1, §6.4: ưu tiên task chưa bảo đảm các tuyên bố về độ trễ và ownership.**

    `tcpip=24`, `eth_rx=22` đều cao hơn `serial_rx=12`; do đó câu “byte UART không bao giờ phải chờ việc mạng” không được bảo đảm. LTE ở priority 5 còn nhạy hơn với việc đó.

    Chuyển log từ nhiều task vào một StreamBuffer cũng cần serialization. StreamBuffer không thay thế trực tiếp queue nhiều producer; nó còn không giữ ranh giới frame UART vốn được console ESP32 gửi thành từng WebSocket message.

    **Rủi ro — suy luận:** starvation/overrun, dữ liệu console bị xen hoặc mất ranh giới.

    **Sửa spec:** đặt giới hạn xử lý mỗi lượt mạng, đo độ trễ phục vụ UART; xác định producer và framing của từng queue. Mỗi `mg_mgr` phải chỉ được task sở hữu nó gọi. Không giữ mutex outbox khi gọi transport; quy định thứ tự khóa bus/cache/store và giới hạn chờ. Nguồn: [FreeRTOS StreamBuffer](https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/main/include/stream_buffer.h), [console.c:123](/home/linh/projects/SBIOT-Gateway/firmware/components/webapi/console.c:123).

18. **Trung bình — §5.5: hợp đồng allocator và `store_get_json()` còn thiếu.**

    `pvPortMalloc/vPortFree` không trực tiếp cung cấp `realloc`; wrapper cần biết kích thước cũ, giữ alignment, xử lý overflow và không mất block cũ khi thất bại. Cần kiểm cả đường newlib reentrant của toolchain.

    Spec đổi `store_get_json()` thành trả con trỏ cache, trong khi API cũ **free con trỏ nhận được** tại [api.c:143](/home/linh/projects/SBIOT-Gateway/firmware/components/webapi/api.c:143).

    **Rủi ro:** free nhầm cache, use-after-free hoặc cấp phát lọt khỏi vùng dự kiến.

    **Sửa spec:** quy định API trả borrowed/owned rõ ràng, thời gian hiệu lực và cách đồng bộ cache; khởi tạo heap/hooks trước mọi cấp phát. Kiểm symbol map và allocator bằng các đường malloc/calloc/realloc, cJSON print, mbedTLS và Mongoose.

19. **Trung bình — §11.2, §11.4: watchdog chưa phát hiện các vòng chờ kẹt.**

    Datasheet SGM706 nêu timeout **min 1,0 s**, typ 1,6 s; không cần để giá trị min ở trạng thái chưa biết. Schematic trang 12 nối WDO về MR và WDI qua transistor đúng với cơ chế reset dự kiến.

    Nhưng bootloader kick trong mọi vòng poll có thể nuôi watchdog vô hạn khi flash không hoàn tất. App `wdog` chạy được cũng không chứng minh các task dịch vụ còn tiến triển.

    **Rủi ro — suy luận:** thiết bị kẹt mà không reset/rollback.

    **Sửa spec:** tất cả thao tác flash có deadline và đường thất bại; watchdog chỉ tiếp tục được nuôi khi thao tác còn trong deadline. Với app, ít nhất cần giám sát tiến trình init/TRIAL. Chốt đúng suffix SGM706 trên BOM vì các suffix có ngưỡng reset khác nhau. Nguồn: [SGM706 datasheet, trang 4](https://www.sg-micro.com/rect/assets/5ee5bbf9-853c-495c-8b9f-fd25cb6f143f/SGM706.pdf).

20. **Trung bình — §3, §6.2, §11.2: cấu hình flash XIP và trạng thái relay cần hợp đồng rõ hơn.**

    Bank/pin QSPI đúng, nhưng “khởi tạo QSPI” chưa xác định QE, read opcode, dummy cycles, clock/prescaler và xử lý flash còn ở trạng thái khác sau reset MCU. GD25Q32E cần QE cho quad operation. Nguồn: [GD25Q32E §4.1, §6](https://www.mouser.com/datasheet/2/870/gd25q32e_rev1_3_20221013-1825518.pdf).

    Với 74HC573, OE đang bị kéo thấp, latch không có reset. Firmware chỉ bảo đảm relay nhả **sau khi đã ghi latch**, không bảo đảm từ lúc cấp nguồn. Khi MCU reset nhưng nguồn latch còn, đầu ra cũ cũng có thể tồn tại trước bootloader.

    **Sửa spec:** định nghĩa trạng thái bàn giao XIP có thể kiểm chứng; trước ghi latch nên disable OE, đặt data, latch rồi enable. Ghi rõ cửa sổ không bảo đảm của board hiện tại; giữ đề xuất đổi pull-up OE là thay đổi phần cứng tùy chọn. Nguồn: [74HC573 datasheet](https://assets.nexperia.com/documents/data-sheet/74HC_HCT573.pdf), schematic trang 11.

21. **Thấp — §4, §7.2, §10.2, §13: danh sách khác biệt chưa đầy đủ/chính xác.**

    Các điểm cần sửa:
    - Body cấu hình ngoài Edge giảm từ **96 KiB xuống 6 KiB**, chưa ghi §13: [web_priv.h:8](/home/linh/projects/SBIOT-Gateway/firmware/components/webapi/web_priv.h:8).
    - ESP32 rollback sau reset trước confirm; STM32 cho nhiều lần TRIAL, chưa mô tả khác biệt này: [test_ota.py:65](/home/linh/projects/SBIOT-Gateway/firmware/test/qemu/test_ota.py:65).
    - ESP32 đã thử CMUX rồi fallback PPP, không phải STM32 mới có: [modem.c:122](/home/linh/projects/SBIOT-Gateway/firmware/components/netmgr/modem.c:122).
    - Schema `port2` đã tồn tại và bị giới hạn bởi hardware environment: [cfgmodel.c:178](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:178).
    - “Chuỗi chứng chỉ, CA, hostname vẫn kiểm đủ” phải giới hạn cho `tlsVerify≥1`; `tlsVerify=0` chủ động không verify.
    - §9.1 “chỉ ghi khi lưu cấu hình” chưa bao gồm boot counter phục vụ `[boot N …]`: [store.c:219](/home/linh/projects/SBIOT-Gateway/firmware/components/store/store.c:219).

    **Rủi ro:** test và kế hoạch port dùng sai chuẩn hành vi. **Sửa spec:** cập nhật §13 và ghi chính xác điều kiện áp dụng.

Một số phần khác đã có nền tảng hợp lý nhưng chưa thể đánh dấu đạt:

- **DMA/cache:** chép TX vào buffer riêng và RX non-cacheable là lựa chọn hợp lý. Cần bổ sung alignment, memory barrier khi bàn giao descriptor, vòng đời zero-copy pbuf và chống reuse TX buffer trước DMA hoàn tất. `LWIP_NETIF_TX_SINGLE_PBUF=1` không tự thực hiện việc chép đó. ADC phải chọn rõ polling hay DMA và engine tương ứng.
- **Modbus CRC/MBAP:** mã được tái sử dụng có CRC `0xA001`, initial `0xFFFF`, low-byte trước; parser MBAP kiểm Protocol ID và Length, giữ Transaction ID. Chưa thấy sai ở những phần đã đọc: [modbus.c:12](/home/linh/projects/SBIOT-Gateway/firmware/components/proto/modbus.c:12), [modbus.c:84](/home/linh/projects/SBIOT-Gateway/firmware/components/proto/modbus.c:84). Đối chiếu [Modbus TCP implementation guide](https://www.modbus.org/file/secure/messagingimplementationguide.pdf). Lỗ hổng chính nằm ở timing/driver mới.
- **MQTT 3.1:** `MQIsdp`, level 3 đúng, nhưng test CONNECT từng byte chưa đủ chứng minh tương thích client. Cần kiểm Remaining Length, CONNACK, Client ID và reconnect/QoS trên broker hỗ trợ 3.1. Nguồn: [IBM MQTT 3.1 specification](https://public.dhe.ibm.com/software/dw/webservices/ws-mqtt/mqtt-v3r1.html).
- **TLS:** IN 16 KiB/OUT 4 KiB là cấu hình có cơ sở; không cần nhận xét rằng OUT 4 KiB tự phá TLS. Cần chốt phiên bản mbedTLS 3.x, nguồn entropy, hàm giờ và tập cipher. Nguồn: [mbedTLS 3.6.3 SSL API](https://mbed-tls.readthedocs.io/projects/api/en/v3.6.3/api/file/ssl_8h/).
- **Ưu tiên ETH/LTE:** chọn route theo link có địa chỉ tương đương [netmgr.c:34](/home/linh/projects/SBIOT-Gateway/firmware/components/netmgr/netmgr.c:34). Nó không phát hiện uplink mất Internet khi địa chỉ vẫn còn. Spec nên nói rõ điều này, cách chọn DNS và việc kết nối TCP/TLS phải reconnect sau đổi đường. Thứ tự `netmgr` trước `serial` cũng cần nói rõ UART3 do LTE tự khởi tạo hay phụ thuộc serial chung.
- **An ninh:** thay đổi đáng ghi nhận so với ESP32 là MQTT bỏ kiểm ngày khi chưa có giờ, đã được D11 và §13 chấp nhận. Cần test chỉ bỏ đúng hai cờ ngày và kiểm ngày trở lại cho handshake sau khi có giờ. Tôi không mở lại F01 và không suy diễn rằng spec đã đạt IEC 62443/ETSI EN 303 645.

Về kiểm thử, §12 chọn đúng hướng host test + HIL, nhưng cần sửa các tiêu chí sau trước khi dùng làm gate:

| Nhóm | Cần bổ sung hoặc làm rõ |
|---|---|
| Boot/OTA | Cắt điện giữa erase/program record trạng thái, chuyển pha, tăng trial count, confirm và restore; backup/staging hỏng; image ngắn/dài sát giới hạn; lỗi flash; mất điện liên tiếp khi restore. Mô phỏng NOR phải có ghi dở, không chỉ ngắt giữa các lời gọi. |
| Arena/Edge | Sparse input → normalized output lớn; va chạm hash; nhiều lỗi đồng thời; allocator tuyến tính thật; reset arena sau compile template; POST/GET xen kẽ; GC và orphan generations. |
| Serial | Chèn khoảng hở t1.5–t3.5 bằng thiết bị phát có timing kiểm soát; DMA wrap, HT/TC, parity/overrun; DE tới stop bit cuối; không mất RX sau RTO. |
| MQTT/CMUX | Mất kết nối ở từng bước QoS 1/2; CleanSession/Session Present; transport trả partial/AGAIN/EOF; DLC negotiation/reset; PPP dài liên tục khi HTTP/TLS đang tải lớn. |
| Tải RAM | Tách kịch bản **2×16 TCP-server client** và **3 TLS client session**: hai cổng không đồng thời ở cả TCP-server và TCP-client/HTTPS. Thêm HTTP client chậm, outbox đầy, TLS handshake đồng thời và flash GC. |
| HIL vật lý | USB-RS485 bọc socket phù hợp test chức năng, chưa đủ đo chính xác t1.5/DE. Cần logic analyzer/oscilloscope, nguồn dòng AI và fixture DI/DO. Đo WDI ngay trên chân trong cả bootloader và app. |

D7 chỉ yêu cầu E2E UART1 nên không cần mở rộng toàn bộ suite sang UART2. Tuy vậy, vẫn cần phép đo driver/pinmux/DE UART2 riêng và kiểm hai bus hoạt động đồng thời. Relay USB và LAN chưa đủ cho AI, DI/DO, timing và quan sát trạng thái bootloader. Hai mươi lần cắt nguồn ngẫu nhiên là smoke test HIL hữu ích, không thay thế fault injection có chủ đích.

**Những việc phải sửa trước khi chốt kế hoạch:** startup XIP; timing EC600U; state machine/erase geometry OTA và rollback khi startup lỗi; ownership/reset arena và giới hạn dữ liệu chuẩn hóa; RTU timing; hợp đồng MQTT/CMUX; cuối cùng là ngân sách RAM và gate đo được. Sau các chỉnh sửa này, spec có thể làm đầu vào triển khai mà vẫn giữ nguyên các quyết định D1–D12.