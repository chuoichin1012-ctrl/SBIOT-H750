QUYẾT ĐỊNH: APPROVED WITH CONDITIONS

Được chuyển sang lập kế hoạch triển khai. Các lỗi còn lại có thể sửa cục bộ, không cần mở lại D1–D12. Tuy nhiên, phải sửa các điều kiện **S1–S7 trong spec trước khi chốt kế hoạch**; những việc **P1–P3 được phép giải quyết trong kế hoạch**.

Tôi đã đối chiếu `48a8e63..746186b`, spec tại `746186bbbc0f1a77498bd42654be1de8cb672413`, follow-up, mã ESP32 và các nguồn dưới đây. Chỉ đọc, không thay đổi tệp. Đây là phê duyệt đầu vào lập kế hoạch, chưa phải xác nhận firmware hoặc HIL đạt.

**1. Kết quả từng phát hiện**

“Đạt” dưới đây là đạt ở mức đặc tả. Các mục chưa đạt dẫn tới điều kiện sửa cụ thể ở phần 2.

| Mục vòng 2 | Kết quả | Nhận xét |
|---|---|---|
| R2-01 — OTA | **Chưa đạt** | §11.2 đã bổ sung kiểm trước mọi lần nhảy, bộ đếm bền vững và nhánh RESTORE lỗi. Nhưng nhánh hết lượt thử WRITE và cách xử lý deadline còn mâu thuẫn với yêu cầu phục hồi/test. **S1**. |
| R2-02 — MQTT | **Chưa đạt** | Phân công coreMQTT/outbox và xử lý MQTT 3.1 đã rõ. Tuy nhiên §8 đòi không nhân đôi QoS 2 cả khi mở phiên sạch mới, không phù hợp chính sách đưa tin đang dở về QUEUED. **S2**. |
| R2-03 — ADC | **Chưa đạt** | Cấu hình chính và hai nửa buffer đã xử lý vấn đề vòng 2. Phương án dự phòng sampling 387,5 cycle tại §6.6 lại vượt chu kỳ trigger. **S3**. |
| R2-04 — CMUX/PPP | **Chưa đạt** | Đã sửa phân đoạn PPP, overhead và xử lý RX `ERR_MEM`. Nhưng FCoff đang áp cả DLC0; hợp đồng lỗi callback TX chưa đúng lwIP. **S4**. |
| R2-05 — cấu hình nhỏ | **Chưa đạt** | Kiểm cả input và normalized output đã bảo vệ round-trip. Nhưng yêu cầu “mọi cấu hình cực đại theo schema ≤ 10 KB” trong §9.3/§12.1 không đúng. **S5**. |
| R2-06 — RAM/report | **Đạt** | Đã có giới hạn thực bằng `edge_tmpl_size() ≤ 26 KB`, đường từ chối và kịch bản tải. Cần thống nhất cách hạch toán heap/tĩnh theo **S7**, rồi chốt số thực trong **P1**. |
| R2-07 — ưu tiên task | **Đạt** | Đã nâng các task rút UART lên trên mạng, giới hạn công việc và thay yield bằng block thật ở ETH. Tham số thực thi và chứng minh dưới tải thuộc **P2**. |
| R2-08 — EC600U | **Chưa đạt** | Các xung 2,5 s / 3,5 s / 200 ms phù hợp nguồn được chọn. Quy tắc bật lại sau “AT ngừng trả lời + 2 s” chưa có cơ sở. **S6**. |
| R2-09 — nghiệm thu ADC | **Đạt** | Đã có điều kiện nhiệt độ, hiệu chuẩn, sai số, nhiễu, ảnh hưởng chéo, độ trễ và phục hồi đo được. Chưa có bằng chứng bench; điều đó không chặn lập kế hoạch. |
| R2-10 — nhất quán | **Đạt** | Các sửa được liệt kê ở follow-up đã hiện diện: vị trí DMA, MPU/startup, allocator template, timeout, erase rounding, overhead, `port2`, DTCM. Các lỗi mới được tách bên dưới. |

| Mục vòng 1 còn mở | Kết quả | Nhận xét |
|---|---|---|
| #2 — timing modem | **Chưa đạt** | §7.2: còn điều kiện **S6**, nguồn Quectel bên dưới. |
| #4 — state machine OTA | **Chưa đạt** | §11.2 và §12.1 còn xung đột **S1**. |
| #11 — MQTT transport/outbox | **Chưa đạt** | Adapter đã đúng; giới hạn bảo đảm qua phiên mới và tiêu chí kiểm thử còn sai, **S2**. |
| #12 — PPP/CMUX | **Chưa đạt** | §7.2 còn lỗi DLC0 và callback TX, **S4**. |
| #15 — RAM | **Đạt** | Đã có giới hạn report/network buffer và gate theo vùng. Không đồng nghĩa ngân sách đã được chứng minh bằng binary; phần đó thuộc **P1**. |
| #16 — bảng pool lwIP | **Chưa đạt** | §5.6 vẫn chưa có bảng số lượng/kích thước/vùng đặt. **Được phép hoàn tất trong kế hoạch**, không dùng mục này để chặn phê duyệt. **P1**. |
| #17 — ưu tiên/ownership | **Đạt** | §6.1/§6.4 đã sửa hướng ưu tiên, producer và đường bàn giao. Cần hoàn thiện **P2** khi lập kế hoạch. |
| #19 — watchdog | **Đạt** | Đã có deadline, bộ đếm checkpoint qua reset và giám sát init/TRIAL. Mâu thuẫn lựa chọn hành động khi SPI4 timeout được xử lý trong **S1**. |

**2. Những lỗi còn lại và lỗi mới**

**S1 — Cao: các nhánh OTA chưa thống nhất với khả năng rollback và tiêu chí test.**

Tại [§11.2](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:834):

- `attempts > 3` trong `WRITE` đi thẳng vào cứu hộ, dù backup có thể còn hợp lệ và flash vẫn hoạt động. Ví dụ có thể kiểm được: ba lần mất nguồn giữa cùng checkpoint, sau đó nguồn ổn định. Nhánh hiện tại không thử rollback.
- §12.1 lại chỉ cho phép cứu hộ khi cả hai image thật sự hỏng. Vì vậy state machine và tiêu chí đạt không thể đồng thời đúng.
- SPI4 quá deadline khi ghi record vừa thuộc nhánh “chạy app hợp lệ ở degraded”, vừa thuộc quy tắc “ngừng kick để reset”. Chưa có thứ tự ưu tiên.

**Sửa spec:** khi WRITE hết lượt thử, chuyển sang RESTORE nếu backup hợp lệ và đường flash còn sử dụng được. Quy định rõ trường hợp lỗi phần cứng khiến không thể phục hồi thì được cứu hộ dù backup còn đúng SHA; sửa gate tương ứng. Chọn rõ hành động cho timeout ghi boot record, tránh hai nhánh cùng áp dụng.

Đây là mâu thuẫn nội tại kiểm được từ §11.2 và [gate §12.1](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:911), không phải yêu cầu thêm một kiến trúc OTA khác.

**S2 — Trung bình: bảo đảm MQTT vượt quá phạm vi của phiên.**

[§8](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:560) yêu cầu không nhân đôi QoS 2 khi reconnect với cả `cleanSession=0` và `1`, nhưng sau phiên mới lại đưa mọi tin đang dở về QUEUED.

Phản ví dụ: broker đã chuyển tin QoS 2 tới ứng dụng, PUBCOMP chưa tới gateway thì mất kết nối. Phiên sạch mới xóa trạng thái cũ; gateway phát lại payload với packet ID mới. **Suy luận từ state machine:** ứng dụng có thể nhận payload lần nữa. MQTT không cung cấp bảo đảm exactly-once xuyên qua các phiên độc lập. [OASIS MQTT 3.1.1, §3.1.2.4 và §4.1](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html).

Ngoài ra, “SUBSCRIBE lặp lại là vô hại” không chính xác: broker phải gửi lại retained message phù hợp. Hành vi đăng ký lại có thể giữ để tương đương ESP32, nhưng phải mô tả đúng tác dụng. [OASIS §3.8.4](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html).

**Sửa spec:** giới hạn kiểm “không nhân đôi QoS 2” vào trường hợp phiên được duy trì; với phiên mới, kiểm đúng chính sách phát lại đã chọn và ghi rõ khả năng trùng payload. Thêm trường hợp retained message khi resubscribe.

Việc dùng `cleanSession=0` làm chính sách giữ state phía client MQTT 3.1 là chấp nhận được; đó không phải bằng chứng broker còn state. Điểm vá trước `handleSessionResumption` đã đúng vị trí. [MQTT 3.1 §4.2](https://public.dhe.ibm.com/software/dw/webservices/ws-mqtt/mqtt-v3r1.html), [coreMQTT 2.3.1](https://raw.githubusercontent.com/FreeRTOS/coreMQTT/v2.3.1/source/core_mqtt.c).

**S3 — Cao: phương án tăng sampling ADC không chạy đủ 1 kHz.**

[§6.6](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:395) cho phép tăng sampling lên 387,5 cycle và khẳng định vẫn vừa 1 ms. Chính công thức của spec cho:

```text
64,5 cycle:   2 × 16 × (64,5 + 8,5) / 10 MHz = 233,6 µs
387,5 cycle:  2 × 16 × (387,5 + 8,5) / 10 MHz = 1.267,2 µs
```

Phương án thứ hai không thể hoàn thành mỗi scan trước trigger kế tiếp. **Suy luận:** khi dùng nó, không còn bảo đảm 100 cặp mẫu tương ứng 100 ms; các tiêu chí cửa sổ và triệt nhiễu cũng bị ảnh hưởng. Các mức sampling được xác nhận trong [ST LL ADC](https://raw.githubusercontent.com/STMicroelectronics/stm32h7xx-hal-driver/master/Inc/stm32h7xx_ll_adc.h).

**Sửa spec:** bỏ phương án dự phòng này ở clock 10 MHz, hoặc thay bằng tổ hợp clock/sampling có thời gian toàn scan dưới 1 ms và `BOOST` phù hợp. Cấu hình chính 10 MHz, `BOOST=01` không có lỗi này. [DS12556 Rev 8, bảng 172](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf).

**S4 — Cao với DLC0; Trung bình với callback TX: CMUX còn hai hợp đồng sai.**

Tại [§7.2](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:499):

- FCoff dừng luồng tổng **ngoại trừ kênh điều khiển DLC0**. Câu “mọi DLC” sai. **Suy luận:** áp nguyên văn có thể chặn chính response/control cần để vận hành flow control. [3GPP TS 27.010 §5.4.6.3.6](https://www.etsi.org/deliver/etsi_ts/127000_127099/127010/19.00.00_60/ts_127010v190000p.pdf).
- Hàng đợi TX đầy không thể trực tiếp trả `ERR_MEM` qua callback PPPoS: `pppos_output_cb_fn` trả **số byte `u32_t`**. lwIP kiểm số byte khác độ dài yêu cầu và chuyển thành `ERR_IF`. [Khai báo PPPoS](https://raw.githubusercontent.com/lwip-tcpip/lwip/master/src/include/netif/ppp/pppos.h), [xử lý output](https://raw.githubusercontent.com/lwip-tcpip/lwip/master/src/netif/ppp/pppos.c).

**Sửa spec:** ghi rõ DLC0 vẫn hoạt động khi FCoff; test response và FCon trong trạng thái đó. Với TX đầy, mô tả callback trả số byte và hành vi bỏ/giữ dữ liệu đúng API; không hứa cơ chế `ERR_MEM` mà callback không cung cấp.

**S5 — Trung bình: 10 KB là giới hạn chấp nhận, không phải cực đại của schema.**

Kiểm trước khi lưu tại [§9.3](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:644) đã giải quyết round-trip. Không cần tăng giới hạn chỉ để xử lý nhận xét này.

Nhưng schema cho `identifier` dài 64 và `delimiter` dài 16, kiểm bằng `strlen`, không cấm ký tự điều khiển. Với 15 topic, mỗi topic có:

- `topic`: 128 ký tự `t`;
- `identifier`: 64 ký tự U+0001;
- `delimiter`: 16 ký tự U+0001;
- các trường khác bỏ qua để lấy mặc định;

JSON compact input dài **9.781 byte**, normalized output dài **10.921 byte**, vượt cả 10.000 lẫn 10.240 byte. Nguồn: [schema](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:59), [kiểm chuỗi](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:275), [defaults](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:346).

Đây là phép tính bằng script theo schema/defaults đã đọc, **không phải chạy binary `cfg_validate`**.

**Sửa spec:** thay gate “cực đại schema luôn ≤ 10 KB” bằng hai nhóm: cấu hình được chấp nhận phải round-trip; normalized output vượt giới hạn phải nhận 413 và không đổi cấu hình đã lưu. Ghi rõ giới hạn bằng byte, ví dụ `10 × 1024`.

**S6 — Cao: mốc tắt xong EC600U chưa đúng.**

[§7.2](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:458) dùng “2 s kể từ khi AT không còn phản hồi”, dẫn §3.7.

Nguồn đã chọn đặt tắt nguồn tại **§3.8.2**, hình 16 có quá trình power-down và ghi chú thời gian phụ thuộc việc rời mạng. Không có quy tắc lấy mất phản hồi AT làm bằng chứng đã tắt xong rồi cộng 2 s. §3.7 là phần nguồn điện. [Quectel EC600U QuecOpen Hardware Design V1.3, trang 45](https://developer.quectel.com/wp-content/uploads/2024/09/Quectel_EC600U%E7%B3%BB%E5%88%97_QuecOpen_%E7%A1%AC%E4%BB%B6%E8%AE%BE%E8%AE%A1%E6%89%8B%E5%86%8C_V1.3.pdf).

**Sửa spec:** định nghĩa điều kiện hoàn tất shutdown theo firmware AT thực dùng và đường timeout/recovery khi không xác nhận được. Không phát xung bật lại chỉ dựa trên sự im lặng của UART. Việc đo thời gian AT sẵn sàng và tinh chỉnh deadline 15 s có thể để cho kế hoạch/bench.

**S7 — Thấp: bảng RAM đang trộn ngân sách tĩnh với cấp phát từ heap.**

Tổng các khoản AXI đang liệt kê là `467 KiB`, dư `45 KiB`: **phép cộng đúng**. Tuy nhiên [§5.3](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:154) cộng report 26 KB như khoản riêng và nói buffer lớn cấp tĩnh, trong khi [§9.4](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:710) nói report cấp từ heap 256 KB.

**Sửa spec:** ghi rõ report nằm trong heap hay section riêng; phân biệt dung lượng heap dành sẵn và mức dùng bên trong heap. Không tính một allocation hai lần. Đây là lỗi hạch toán, **chưa phải bằng chứng thiếu RAM**.

Về ba điểm được yêu cầu chú ý thêm:

- **Ưu tiên `serial_rx=26`, `cmux_rx=25`:** không tự gây deadlock hoặc starvation. Với lượng việc hữu hạn rồi block, đây là cách hợp lý để bảo vệ ring UART. Tuy nhiên notify đang pending có thể khiến lần “chờ ISR” trả về ngay; retry 1 ms cũng phải thực sự block, không busy-wait. Cần chốt quantum, tick và điều kiện nhường CPU trong **P2**. [FreeRTOS scheduler](https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/main/tasks.c). `pppos_input_tcpip` có đường enqueue sang `tcpip`; khi đầy trả lỗi, nên việc xử lý backpressure là cần thiết. [lwIP `tcpip_inpkt`](https://raw.githubusercontent.com/lwip-tcpip/lwip/master/src/api/tcpip.c).
- **Factory record:** layout đúng: backup kết thúc tại `0x280000 + 0x170000 = 0x3F0000`; vùng factory kết thúc tại `0x400000`. Không chồng lấn. Nhưng `0x3F0000` là **offset trong chip SPI4**, không phải địa chỉ CPU để SWD ghi trực tiếp. Cần loader/routine chạy trên MCU để truy cập SPI4; đây là công việc kế hoạch, không phải lỗi kiến trúc. [ST — lập trình bộ nhớ ngoài](https://dev.st.com/stm32cube-docs/prog/2.23.0/en/docs/markup/CubeProg_UserManual/Memory_programming_and_erasing.html).
- **Report 26 KB:** phải dùng kết quả `edge_tmpl_size()`, không biến “khoảng 100 slot” thành giới hạn thay thế. Spec hiện đã chọn đúng cách kiểm theo byte. [Hàm tính kích thước hiện có](/home/linh/projects/SBIOT-Gateway/firmware/components/proto/edgecore.c:407).

**3. Điều kiện phê duyệt**

**Phải sửa trong spec trước khi chốt kế hoạch:**

| Điều kiện | Kết quả kiểm được |
|---|---|
| **S1** | Bảng OTA có nhánh WRITE hết lượt thử, RESTORE bất khả thi và SPI4 timeout không mâu thuẫn; gate fault injection khớp từng nhánh. |
| **S2** | Test MQTT phân biệt phiên được duy trì với phiên mới; ghi nhận retained message được gửi lại khi resubscribe. |
| **S3** | Mọi cấu hình ADC được cho phép đều có phép tính thời gian scan dưới 1 ms; bỏ khẳng định sai về 387,5 cycle ở 10 MHz. |
| **S4** | FCoff chừa DLC0; hợp đồng TX đầy đúng kiểu trả về của PPPoS; có test hai trường hợp. |
| **S5** | Giới hạn JSON ghi bằng byte; test oversized normalized output mong đợi 413, không yêu cầu toàn bộ schema phải vừa 10 KB. |
| **S6** | Có điều kiện shutdown-complete và timeout/recovery có nguồn; bỏ mốc “AT im lặng + 2 s” và sửa tham chiếu. |
| **S7** | Report và các buffer được phân loại rõ heap/tĩnh, mỗi allocation chỉ tính một lần. |

**Được phép giải quyết trong kế hoạch triển khai:**

- **P1 — RAM/lwIP:** bảng đầy đủ pool, mailbox, buffer, số lượng, kích thước và vùng đặt; chứng minh ngân sách SRAM1 128 KiB và các vùng khác. Đưa linker-map và hai kịch bản §12.3 thành gate triển khai. **#16 không chặn việc bắt đầu lập kế hoạch.**
- **P2 — scheduling/ADC:** chốt lượng việc mỗi lượt, block/backpressure, tick, giới hạn thời gian rút ring; cách lấy nhất quán `ready_half/seq` và xử lý notify cũ sau phục hồi. Kế hoạch kiểm phải bao gồm UART liên tục, PPP, LAN flood và tiến triển của các task thấp hơn.
- **P3 — dữ liệu sản xuất/handoff:** định dạng/version/CRC và cách chọn hai bản factory record; loader SPI4, readback, test mất nguồn và giữ record qua factory reset/OTA. Đồng thời xác định cách truyền cờ `degraded` sang app mà không phụ thuộc việc ghi được SPI4 đang lỗi.

Các điều kiện này đủ cụ thể để theo dõi trong kế hoạch; không yêu cầu phải hoàn thành firmware, loader hay bench trước khi được lập kế hoạch.