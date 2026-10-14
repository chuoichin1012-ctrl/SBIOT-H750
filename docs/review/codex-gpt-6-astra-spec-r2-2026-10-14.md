QUYẾT ĐỊNH: NOT APPROVED

Bản duyệt: [spec tại HEAD `48a8e63`](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md), đối chiếu review vòng 1, follow-up và lịch sử bốn commit của spec. Tôi giữ nguyên D1–D12, chỉ đọc và không tạo, sửa hay xóa tệp.

Đây là review tĩnh, chưa có build hoặc HIL. **13 phát hiện vòng 1 đã được xử lý ở mức đặc tả; 8 phát hiện chưa đủ.** Thiết kế ADC khả thi, nhưng hợp đồng lấy dữ liệu và phục hồi lỗi chưa bảo đảm kết quả đúng.

## 1. Kiểm từng phát hiện vòng 1

“Đã xử lý” dưới đây nghĩa là spec đã giải quyết vấn đề được nêu, không đồng nghĩa phần triển khai đã được kiểm chứng.

| # | Kết luận | Đối chiếu |
|---|---|---|
| 1 | **Đã xử lý** | §6.2 đã yêu cầu startup và `SystemInit` riêng, trạng thái bàn giao và kiểm disassembly/cold boot/warm boot. Còn sự lặp việc khởi tạo giữa §6.2–6.3, ghi riêng ở mục 3. |
| 2 | **Xử lý chưa đủ** | §7.2 đã sửa PWRKEY thành 2,5 s, nhưng thời gian chờ UART, xung reset và timing tắt bằng PWRKEY vẫn chưa chốt. Tài liệu Quectel xác định reset ≥100 ms, tắt bằng PWRKEY ≥3 s; cần khóa đúng phiên bản và mốc tính thời gian. Nguồn: [Quectel V1.3, §3.8](https://developer.quectel.com/wp-content/uploads/2024/09/Quectel_EC600U%E7%B3%BB%E5%88%97_QuecOpen_%E7%A1%AC%E4%BB%B6%E8%AE%BE%E8%AE%A1%E6%89%8B%E5%86%8C_V1.3.pdf). |
| 3 | **Đã xử lý** | §5.2: hai slot bắt đầu tại `0x110000`, `0x280000`, cùng dài `0x170000`; đều căn 64 KiB. Checkpoint, erase và program đã được tách. |
| 4 | **Xử lý chưa đủ** | §11.2 còn thiếu nhánh **RESTORE hoàn tất nhưng SHA sai**, thiếu cách lưu số lần thử qua reset, và không còn quy tắc rõ ràng kiểm image trước **mọi** lần nhảy, nhất là `TRIAL`. Đây là khoảng trống trực tiếp trong [bảng trạng thái](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:749); xem R2-01. |
| 5 | **Đã xử lý** | §11.3 đã có rollback khi khởi tạo lỗi, deadline init/TRIAL và phân biệt lỗi khởi tạo với mất kết nối Internet. |
| 6 | **Đã xử lý** | §9.3, §9.4 yêu cầu dữ liệu tồn tại lâu phải ra khỏi arena; template biên dịch bằng heap; có poison test. Đúng với ownership trong [edgecore.c](/home/linh/projects/SBIOT-Gateway/firmware/components/proto/edgecore.c:358). |
| 7 | **Đã xử lý** | Với **Edge**, §9.4 đã giới hạn cả input và normalized output ở 48 KiB, kiểm trước đổi generation. Tuy nhiên lỗi tương tự còn tồn tại ở cấu hình nhỏ — R2-05. |
| 8 | **Đã xử lý** | §9.3 chuyển sang TLSF có thu hồi, giải phóng từng node; §12.1 yêu cầu đo bằng chính allocator đó, gồm phân mảnh. |
| 9 | **Đã xử lý** | Arena chỉ tồn tại trong callback đồng bộ; body Edge có ownership riêng, 503 khi bận, giải phóng khi close/timeout và kiểm byte thực nhận. |
| 10 | **Đã xử lý** | §6.5 đã thêm t1.5/t3.5, timing cố định trên 19.200 baud, ISR LL không abort circular DMA; §12 có kiểm timing vật lý. Phù hợp hướng xử lý của [Modbus Serial V1.02 §2.5.1.1](https://www.modbus.org/file/secure/modbusoverserial.pdf). Cách phát hiện ký tự mới bằng phần cứng vẫn phải cụ thể hóa trong kế hoạch driver. |
| 11 | **Xử lý chưa đủ** | §8 sửa đúng adapter và các trạng thái QoS, nhưng áp dụng `Session Present` chung cho MQTT 3.1 và 3.1.1. MQTT 3.1 không có cờ này; coreMQTT dùng kết quả đó để phục hồi/xóa state. Nguồn: [MQTT 3.1](https://public.dhe.ibm.com/software/dw/webservices/ws-mqtt/mqtt-v3r1.html), [coreMQTT 2.3.1](https://raw.githubusercontent.com/FreeRTOS/coreMQTT/v2.3.1/source/core_mqtt.c). Xem R2-02. |
| 12 | **Xử lý chưa đủ** | §7.2 đã bổ sung HT/TC và state machine, nhưng xử lý FCon/FCoff sai phạm vi; chưa quy định khi `pppos_input_tcpip` trả lỗi. Nguồn: [27.010 §5.4.6.3.5–6](https://www.etsi.org/deliver/etsi_ts/127000_127099/127010/19.00.00_60/ts_127010v190000p.pdf), [lwIP `pppos_input_tcpip`](https://raw.githubusercontent.com/lwip-tcpip/lwip/master/src/netif/ppp/pppos.c). Xem R2-04. |
| 13 | **Đã xử lý** | §9.4 đã mô tả ba pha, các phạm vi tên, exposed ID/span, giới hạn 64/5/100, so tên sau hash và khác biệt load/save. Khớp cấu trúc [validator hiện tại](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:391). |
| 14 | **Đã xử lý** | §9.4 có commit manifest cuối, cleanup orphan và ghim generation cho GET. Khi triển khai, “chờ GET” phải là trì hoãn cleanup, không chặn event loop phục vụ GET. |
| 15 | **Xử lý chưa đủ** | §5.3 và §12.3 sửa đúng heap/pool/stack, nhưng ngân sách report khoảng 30 KiB chưa bao phủ hành vi mã tái sử dụng; còn thiếu phân bổ cụ thể cho coreMQTT network buffer và các buffer mới. Nguồn: [edgecore.c:407](/home/linh/projects/SBIOT-Gateway/firmware/components/proto/edgecore.c:407), [edge_rt.c:880](/home/linh/projects/SBIOT-Gateway/firmware/components/services/edge_rt.c:880). Xem R2-06. |
| 16 | **Xử lý chưa đủ** | §5.6 liệt kê đúng các pool cần tính và sửa errno, nhưng **chưa có bảng kích thước/vùng đặt**; việc đó được chuyển sang kế hoạch. Vì vậy chưa chứng minh các khoản SRAM1 trong §5.3 cùng nằm vừa 128 KiB. Nguồn: chính [§5.3–5.6](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:143). |
| 17 | **Xử lý chưa đủ** | MessageBuffer và ownership đã rõ hơn. Tuy nhiên §6.1 vẫn khẳng định task mạng chỉ tăng độ trễ, không gây mất byte; “nhường CPU” sau tám frame chưa bảo đảm task ưu tiên thấp được chạy. Nguồn: [quy tắc ưu tiên FreeRTOS](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/01-Tasks-and-co-routines/03-Task-priorities). Xem R2-07. |
| 18 | **Đã xử lý** | §5.5 bổ sung wrapper realloc/calloc, alignment, khởi tạo heap và giữ `store_get_json()` trả bản sao do caller sở hữu, phù hợp [api.c:143](/home/linh/projects/SBIOT-Gateway/firmware/components/webapi/api.c:143). |
| 19 | **Xử lý chưa đủ** | Deadline flash, giám sát init/TRIAL và timeout watchdog đã sửa đúng. Nhưng §11.2 yêu cầu “thất bại ba lần qua reset” mà chưa đặc tả bộ đếm bền vững; xem R2-01. Timeout 1,0 s được xác nhận từ [SGM706, trang 4](https://www.sg-micro.com/rect/assets/5ee5bbf9-853c-495c-8b9f-fd25cb6f143f/SGM706.pdf). |
| 20 | **Đã xử lý** | §6.2 đã yêu cầu QE/opcode/dummy/prescaler/reset flash; §11.2 có OE-disable trước ghi latch và nêu đúng cửa sổ không bảo đảm của board. |
| 21 | **Đã xử lý** | §13 đã bổ sung các khác biệt chính; §7.3 giới hạn nhận định TLS theo `tlsVerify`; §9.1 có boot counter. Còn vài câu không thống nhất, liệt kê ở mục 3. |

Không có phát hiện vòng 1 nào đủ căn cứ để bác bỏ toàn bộ là “Sai”. Các khẳng định mới sai về ADC/CMUX được nêu riêng dưới đây.

## 2. Review ADC §6.6

### Đường DMA: chọn BDMA đúng, nói “chỉ có BDMA” sai

ADC3 có cả:

- **DMAMUX1 request 115 → DMA1/2**.
- **DMAMUX2 request 17 → BDMA**.

Điều này có trong RM0433, bảng kết nối DMAMUX, và [header LL chính thức của ST](https://raw.githubusercontent.com/STMicroelectronics/stm32h7xx-hal-driver/master/Inc/stm32h7xx_ll_dmamux.h). Giữ BDMA/SRAM4 là hợp lý; sửa §5.4 thành **lựa chọn của thiết kế**, không phải giới hạn của ADC3.

### Oversampling và độ rộng dữ liệu

Cấu hình đáp ứng đúng mục tiêu là:

```text
ROVSE = 1
OVSR  = 15       // tỷ lệ 16 trên ADC của STM32H750
OVSS  = 4
TROVS = 0       // một trigger hoàn thành toàn bộ tích lũy
CONT  = 0
DISCEN = 0
LSHIFT = 0
```

Cụm “chế độ triggered” đang dễ dẫn tới cấu hình ngược: `TROVS=1` yêu cầu trigger cho từng phép chuyển đổi thành phần. HAL gọi lựa chọn cần dùng là `ADC_TRIGGEREDMODE_SINGLE_TRIGGER`. Không áp dụng encoding ADC3 của H72/H73 sang H750. Nguồn: [ST LL ADC — oversampling](https://raw.githubusercontent.com/STMicroelectronics/stm32h7xx-hal-driver/master/Inc/stm32h7xx_ll_adc.h), [ST HAL ADC](https://raw.githubusercontent.com/STMicroelectronics/stm32h7xx-hal-driver/master/Inc/stm32h7xx_hal_adc.h).

Tính toán: `16 × 65535 = 1.048.560`; dịch phải bốn bit còn tối đa `65535`. Vì vậy **half-word cho cả peripheral và memory là phù hợp**, nếu không bật dịch trái hoặc xử lý offset làm thay đổi miền giá trị. `ADC_DR` rộng 32 bit không tự buộc buffer phải là `uint32_t`.

### `DMNGT` và trigger

- `ADC_CFGR.DMNGT = 0b11`: circular DMA.
- BDMA cũng phải bật circular; peripheral không tăng địa chỉ, memory tăng; ring hiện tại có **200 phần tử half-word**, không phải 400 phần tử.
- **TIM6_TRGO** là một lựa chọn hợp lệ, `EXTSEL=13`; đặt TRGO theo update và chọn cạnh kích thích hợp. Không cần dùng TIM2 đang được dành cho `now_us()`.

Nguồn: [RM0433 Rev 8, §25.4.27 và bảng 210](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

### Kernel clock và `BOOST`

PLL2P/PLL3R là nguồn hợp lệ. Spec cần phân biệt clock nguồn với **clock thực sự vào analog ADC**, kể cả bộ chia cố định của revision V.

DS12556 Rev 8 cho các mức tối đa tương ứng:

| `BOOST` | Clock ADC tối đa |
|---|---:|
| `00` | 6,25 MHz |
| `01` | 12,5 MHz |
| `10` | 25 MHz |
| `11` | 50 MHz |

Nhưng **50 MHz không phải bảo đảm chất lượng 16 bit cho mọi package**. AN5354 Rev 2, bảng 12, nêu **19 MHz** cho LQFP100, một ADC, 16 bit trong điều kiện điển hình của bảng. Nguồn: [DS12556, bảng 172](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf), [AN5354, §3.4](https://www.st.com/resource/en/application_note/an5354-getting-started-with-16bit-adc-on-stm32h7-mcus-stmicroelectronics.pdf).

**Đề xuất thiết kế:** chốt clock analog 10 MHz, `BOOST=01`, rồi ghi đầy đủ đường PLL/prescaler. Đây là lựa chọn đề xuất, không phải giá trị duy nhất ST cho phép.

Với lựa chọn đó, hai kênh ×16 ở sampling 64,5 cycle mất khoảng:

\[
2\times16\times(64,5+8,5)/10\,MHz=233,6\,\mu s
\]

Chưa cộng độ trễ trigger, nhưng vẫn có dư địa lớn so với chu kỳ 1 ms.

### Hiệu chuẩn offset và linearity

Chủ trương hiệu chuẩn trước khi chạy ADC đúng. Lời gọi trong spec là dạng rút gọn; nếu dùng làm hợp đồng API, cần ghi:

```c
HAL_ADCEx_Calibration_Start(
    &hadc3, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED);
```

Phải kiểm kết quả; regulator ADC ổn định và ADC ở trạng thái phù hợp trước hiệu chuẩn. Sau reset/deep-power-down làm mất hệ số, phải hiệu chuẩn hoặc nạp lại hệ số trước lấy mẫu. Nguồn: [HAL ADC extension của ST](https://raw.githubusercontent.com/STMicroelectronics/stm32h7xx-hal-driver/master/Src/stm32h7xx_hal_adc_ex.c).

AN5354 phân biệt offset với linearity, có hướng dẫn dùng hệ số linearity từ nhà máy và điều kiện khuyến nghị khi tự hiệu chuẩn. **Không đủ cơ sở để coi chỉ một lời gọi hiệu chuẩn là chứng minh độ chính xác toàn dải/nhiệt độ.** [AN5354 §2.5](https://www.st.com/resource/en/application_note/an5354-getting-started-with-16bit-adc-on-stm32h7-mcus-stmicroelectronics.pdf).

### Sampling time với LM2904

64,5 cycle là cấu hình hợp lệ, nhưng lý do “op-amp trở kháng thấp” chưa đủ để xác nhận settling 16 bit.

[Schematic trang 7](/home/linh/projects/SBIOT-Gateway/docs/Job2.PDF) cho thấy ngõ ADC nối trực tiếp ngõ ra LM2904, có tụ 5 pF; điện trở 100 kΩ thuộc mạng đầu vào/khuếch đại, **không được coi là điện trở nguồn nối tiếp trực tiếp vào ADC**.

Cần đối chiếu sampling time theo clock đã chốt, tải điện dung và đáp ứng op-amp; kiểm hai kênh lần lượt ở 4/20 mA rồi đảo lại để phát hiện ảnh hưởng chuyển kênh. Bảng tham chiếu là [DS12556, bảng 173 — sampling time theo \(R_{AIN}\)](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf). Hiện chưa có bằng chứng bench để khẳng định 64,5 cycle đạt sai số mong muốn.

### MPU 1 KiB trong SRAM4

**Khả thi.** Region cuối SRAM4 là:

```text
0x3800FC00 … 0x3800FFFF
```

Địa chỉ căn 1 KiB. Cần dành riêng toàn region trong linker, cấu hình **Normal non-cacheable**, XN và quyền truy cập thích hợp; nếu chồng region cacheable lớn hơn thì xác định ưu tiên region. Cấu hình phải có hiệu lực trước khi bật DMA. Nguồn: [ST PM0253, §4.6](https://www.st.com/resource/en/programming_manual/pm0253-stm32f7-series-and-stm32h7-series-cortexm7-processor-programming-manual-stmicroelectronics.pdf).

§6.2 hiện chưa liệt kê region này trong trình tự MPU. **Non-cacheable chỉ giải quyết cache coherency; không làm thao tác chép 400 byte trở thành snapshot nguyên tử.**

### Công thức đổi sang mA

Công thức đúng về đơn vị và hệ số danh định:

\[
I_{\rm mA}=\frac{\overline{code}\times3,0}
{65535\times0,499\times0,120}
\]

Từ schematic, hệ số khuếch đại danh định là `49,9 kΩ / 100 kΩ = 0,499`.

Tính lại: 20 mA → **1,1976 V** → **26161,572**, tức khoảng **26162 count**. Con số “khoảng 26.160” trong spec không phải sai lệch đáng kể. Cần dùng phép chia thực hoặc fixed-point có thứ tự nhân/chia đúng; tổng 100 mẫu phải dùng ít nhất `uint32_t`.

Đây là phép tính danh định từ [mạch ADC trang 7](/home/linh/projects/SBIOT-Gateway/docs/Job2.PDF), chưa bao gồm sai số shunt, điện trở, VREF và op-amp.

### Snapshot, OVR và lỗi BDMA

Đây là phần **chưa đạt**:

- CPU chép ring trong khi BDMA tiếp tục ghi có thể lấy dữ liệu thuộc nhiều vòng khác nhau. Task bị preempt giữa lần chép càng làm cửa sổ không xác định. Đây là **suy luận từ truy cập đồng thời được spec mô tả**, chưa phải lỗi đã đo.
- `OVR` và lỗi BDMA không đủ để quy lỗi riêng AI1 hay AI2. Với một scan/ring chung, cần vô hiệu hóa **cả hai kênh** đến khi dựng lại được cửa sổ đầy đủ.
- Khi OVR, ADC có thể ngừng phát DMA request tới khi xử lý cờ; chỉ “khởi động lại” chưa xác định việc phục hồi thứ tự rank/buffer. [RM0433 §25.4.27](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

Một cách sửa đơn giản: **hai nửa, mỗi nửa 100 cặp mẫu**, tổng **800 byte**, vẫn vừa region 1 KiB. HT/TC chỉ xuất hiện mỗi 100 ms, không phải mỗi mẫu. Quy định ownership/thời hạn đọc nửa đã hoàn thành và phát hiện đọc trễ.

Đường lỗi cần ghi rõ: dừng trigger → dừng ADC/BDMA → xóa cờ → đặt lại địa chỉ/count/rank → arm BDMA/ADC → bật trigger cuối; chỉ công bố giá trị sau đủ **100 cặp mẫu mới**, kể cả lần khởi động đầu tiên.

## 3. Lỗi mới và các con số đối chiếu

### Các con số hiện khớp

| Hạng mục | Kết quả tính lại |
|---|---|
| SPI4 | `1024 + 64 + 1472 + 1472 + 64 = 4096 KiB`; không chồng vùng. |
| Image | Slot `0x170000`; header `0x400`; thân tối đa **`0x16FC00 = 1.506.304 byte`**. §5.1 và §11.1 khớp. |
| AXI | `256 + 64 + 48 + 30 + 20 + 33 = 451 KiB`; dư **61 KiB**, phù hợp “khoảng 60”. |
| SRAM2+3 | Buffer chiếm 144/160 KiB; còn 16 KiB trước metadata/alignment. |
| SRAM4 | `32 + 24 + 1 = 57 KiB`; còn 7 KiB. |
| ADC | `100 × 2 × 2 = 400 byte`; đúng 100 ms **nếu** mỗi trigger sinh đủ một cặp kết quả. |

Nguồn số liệu: [§5](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:119), [§11.1](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:707). Các phép cộng này không thay thế linker map hay đo tải.

### Các chỗ không thống nhất

1. **§5.4 mở đầu nói buffer riêng của mọi driver ở SRAM1**, nhưng ngay bảng dưới ADC nằm SRAM4. Câu “không engine nào truy cập TCM” cũng phải giới hạn vào các engine đang chọn; MDMA là ngoại lệ trong kiến trúc H7. [RM0433 §2.1.2](https://www.st.com/resource/en/reference_manual/rm0433-stm32h743-753-and-stm32h750-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf).

2. **§6.2 và §6.3 cùng nhận trách nhiệm MPU/cache/chép ITCM.** Chốt một lần khởi tạo có thứ tự; bổ sung MPU ADC vào đó.

3. **§5.5 ghi biên dịch template dùng arena; §9.3 và §9.4 ghi dùng heap.** Sửa bảng §5.5 theo hợp đồng ownership đã chọn.

4. **§9.3 timeout Edge là 15 s; §10.1 nói HTTP body sau hai lần 15 s.** Cần xác định ngoại lệ hoặc thống nhất thời hạn.

5. **§5.2 nói đơn vị cuối chỉ xử lý phần image; §11.2 nói mỗi checkpoint xóa 16 sector.** Cần phân biệt số byte copy/hash với vùng erase làm tròn sector. Xóa vật lý không thể dừng giữa sector. [GD25Q32E §7.15](https://www.mouser.com/datasheet/2/870/gd25q32e_rev1_3_20221013-1825518.pdf).

6. **§7.2 tính overhead frame CMUX là sáu byte trong khi dùng length hai byte.** Frame độc lập có hai flag, address, control, hai byte length và FCS: **bảy byte**. Buffer 2112 byte vẫn đủ cho 2048 + 7; đây là lỗi mô tả, chưa phải thiếu buffer. [27.010 §5.2.1](https://www.etsi.org/deliver/etsi_ts/127000_127099/127010/19.00.00_60/ts_127010v190000p.pdf).

7. **§10.2 gọi API port2 là “Mới”, §13 nói schema đã có.** Nên ghi “bật trên phần cứng STM32”; follow-up cũng còn dòng “ADC3 polling” đã lỗi thời.

8. Tổng stack DTCM theo §6.1 là **85 KiB**, thêm init/timer thành **93 KiB**, thêm MSP thành **97 KiB**. Chưa tràn 128 KiB, nhưng “khoảng 84 KiB” chưa phản ánh đầy đủ các task mới và chưa tính TCB/newlib/static data.

## 4. Danh sách phát hiện còn lại

### Nghiêm trọng

Không ghi nhận phát hiện mới đủ bằng chứng để xếp mức này.

### Cao

**R2-01 — §11.1–11.2: state machine OTA chưa đóng đầy đủ các đường lỗi.**

- **Mô tả:** thiếu chuyển tiếp khi restore verify thất bại; số lần retry/checkpoint failure chưa có hợp đồng lưu qua reset; bảng `TRIAL` không yêu cầu kiểm image trước nhảy.
- **Rủi ro — suy luận:** retry vô hạn, không đạt chính sách ba lần, hoặc nhảy vào image hỏng trong một nhánh chưa được bảo vệ.
- **Sửa:** thêm invariant kiểm header/size/vector/SHA trước mọi lần nhảy; định nghĩa nhánh restore thất bại; lưu và cập nhật attempt counter rõ ràng, gồm trường hợp SPI4 lỗi. Bổ sung fault-injection tại các điểm cập nhật này.
- **Nguồn:** [bản ghi và bảng OTA hiện tại](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:728).

**R2-02 — §8: hợp đồng session chưa đúng cho MQTT 3.1.**

- **Mô tả:** logic mới dựa hoàn toàn vào `Session Present`, trong khi MQTT 3.1 dành byte đó làm reserved. coreMQTT còn tự phục hồi PUBREL hoặc xóa state trong `MQTT_Connect()`.
- **Rủi ro — suy luận:** coi persistent session là session mới, phát lại giao dịch QoS 2 như PUBLISH mới hoặc làm lệch state giữa outbox và thư viện.
- **Sửa:** đặc tả riêng reconnect 3.1 với `CleanSession=0`, ownership của state/PUBREL và chính sách resubscribe. Kiểm disconnect tại từng pha QoS 1/2 trên broker 3.1; không chỉ kiểm CONNECT.
- **Nguồn:** [MQTT 3.1 — CONNACK](https://public.dhe.ibm.com/software/dw/webservices/ws-mqtt/mqtt-v3r1.html), [MQTT 3.1.1 §3.2.2](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html), [coreMQTT `handleSessionResumption`](https://raw.githubusercontent.com/FreeRTOS/coreMQTT/v2.3.1/source/core_mqtt.c).

**R2-03 — §6.6: chưa bảo đảm cửa sổ ADC nhất quán và phục hồi đúng hai kênh.**

- **Mô tả:** chép ring đang chạy, không có ownership/snapshot; “triggered” chưa chỉ rõ `TROVS=0`; lỗi chung lại mô tả theo “điểm tương ứng”.
- **Rủi ro — suy luận:** sai cửa sổ trung bình, mất khả năng lọc như tuyên bố, hoặc công bố dữ liệu cũ/sai rank sau lỗi.
- **Sửa:** chốt cấu hình thanh ghi và cơ chế bàn giao buffer như mục 2; vô hiệu hóa cả hai AI khi mất đồng bộ; thêm test DMA wrap trong lúc đọc, task bị trì hoãn, OVR, BDMA transfer error và warm-up ban đầu.
- **Nguồn:** [§6.6](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:375), các nguồn ST ở mục 2.

**R2-04 — §7.2: flow control và tính liên tục của PPP còn sai/thiếu.**

- **Mô tả:** FCon/FCoff điều khiển toàn bộ luồng dữ liệu mux, còn MSC áp dụng từng DLC. Spec gộp thành “DLC tương ứng”. Chưa xử lý lỗi enqueue PPP. Khẳng định MRU 1500 luôn vừa frame N1=2048 cũng sai với PPPoS có escape expansion.
- **Rủi ro — suy luận:** gửi khi modem yêu cầu dừng, mất dữ liệu hoặc loại bỏ gói PPP hợp lệ dưới tải.
- **Sửa:** tách flow control toàn mux/từng DLC; quy định phản hồi control command; xử lý `ERR_MEM` có giới hạn; phân mảnh byte stream PPP qua nhiều frame ≤N1. Test payload nhiều `0x7D/0x7E`, pool đầy và FCoff.
- **Nguồn:** [27.010 §5.4.6](https://www.etsi.org/deliver/etsi_ts/127000_127099/127010/19.00.00_60/ts_127010v190000p.pdf), [lwIP PPPoS](https://raw.githubusercontent.com/lwip-tcpip/lwip/master/src/netif/ppp/pppos.c), [RFC1662 §4.2](https://www.rfc-editor.org/rfc/rfc1662.html).

**R2-05 — §9.3, §10.1–10.2: cấu hình nhỏ chưa bảo đảm POST → GET → POST.**

Ví dụ theo schema hiện tại: 16 topic, mỗi phần tử chỉ gửi:

- `topic`: 128 ký tự `t`;
- `identifier`: 64 ký tự backslash;
- `delimiter`: 16 ký tự `d`.

JSON compact đầu vào **5057 byte**; thêm defaults thành **6273 byte**, vượt **6144 byte**. Đây là phép tính từ schema/defaults, không phải kết quả chạy validator binary.

- **Rủi ro:** cấu hình được nhận và lưu nhưng chính kết quả GET không thể POST lại.
- **Sửa:** quy định giới hạn normalized output cho từng cấu hình trước commit, hoặc tăng giới hạn nhận đủ chứa normalized output và cập nhật ngân sách. Thêm test round-trip với escape/default expansion.
- **Nguồn:** [schema topic](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:62), [defaults](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:346), [chuẩn hóa](/home/linh/projects/SBIOT-Gateway/firmware/components/cfgmodel/cfgmodel.c:294).

### Trung bình

**R2-06 — §5.3, §5.6, §12.3: ngân sách chưa bao phủ đầy đủ cấu hình hợp lệ.**

- **Mô tả:** một template 1391 ký tự với 150 trường cùng tham chiếu điểm `p` làm `edge_tmpl_size()` yêu cầu **37.242 byte**, riêng report buffer đã vượt khoản “mô hình/cache/report khoảng 30 KiB”. Phép tính dựa trên mã hiện tại. Các pool lwIP và coreMQTT network buffer cũng chưa được phân bổ đầy đủ.
- **Rủi ro — suy luận:** thiếu RAM khi đưa cấu hình hợp lệ lên thiết bị; ngân sách đang đánh giá thấp peak.
- **Sửa:** chốt kích thước/vùng/ownership của từng allocation lớn; tính report theo số placeholder, không chỉ số điểm; đưa cấu hình cực đại này vào gate bộ nhớ.
- **Nguồn:** [edgecore.c:407](/home/linh/projects/SBIOT-Gateway/firmware/components/proto/edgecore.c:407), [edge_rt.c:880](/home/linh/projects/SBIOT-Gateway/firmware/components/services/edge_rt.c:880).

**R2-07 — §6.1: tuyên bố không mất UART vượt quá bảo đảm của scheduler.**

- **Mô tả:** `taskYIELD()` ở task ưu tiên cao không buộc task thấp hơn được chạy. Ring hữu hạn vẫn có thể tràn khi consumer không được phục vụ.
- **Rủi ro — suy luận:** broadcast/network load làm mất serial/CMUX dù DMA hoạt động.
- **Sửa:** quy định cách giới hạn tải khiến task mạng thực sự block, hoặc một cơ chế khác có budget đo được; đặt giới hạn service latency và tiêu chí overrun dưới tải.
- **Nguồn:** [FreeRTOS task priorities](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/01-Tasks-and-co-routines/03-Task-priorities), [§6.1](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:267).

**R2-08 — §7.2, §14: bảng timing EC600U chưa hoàn tất.**

- **Mô tả:** còn placeholder và tham chiếu phụ thuộc bản Hardware Design/QuecOpen.
- **Rủi ro:** driver chọn sai pulse hoặc kết luận timeout trước khi UART sẵn sàng.
- **Sửa:** ghi document/revision/page, độ dài pulse, mốc bắt đầu và deadline từng bước. Không dùng số mục của bản tài liệu khác.
- **Nguồn:** [Quectel V1.3](https://developer.quectel.com/wp-content/uploads/2024/09/Quectel_EC600U%E7%B3%BB%E5%88%97_QuecOpen_%E7%A1%AC%E4%BB%B6%E8%AE%BE%E8%AE%A1%E6%89%8B%E5%86%8C_V1.3.pdf).

**R2-09 — §6.6, §12.2: chất lượng ADC chưa có tiêu chí nghiệm thu định lượng.**

- **Mô tả:** chưa chốt clock/BOOST theo package, phương pháp hiệu chuẩn và sai số cho phép. “Ổn định”, “triệt nhiễu” chưa đo được thành PASS/FAIL.
- **Rủi ro:** số đọc có vẻ ổn định nhưng sai; oversampling không tự bảo đảm 16 bit hiệu dụng.
- **Sửa:** chốt clock, sampling, sai số mA, độ dao động, độ trễ và mức suy giảm nhiễu 50/60 Hz; kiểm hai kênh ở hai đầu dải và sau phục hồi lỗi. Cửa sổ 100 ms có điểm triệt lý tưởng ở 50/60 Hz; độ lệch tần số, jitter và snapshot ảnh hưởng kết quả thực — đây là **suy luận xử lý tín hiệu**.
- **Nguồn:** [DS12556](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf), [AN5354](https://www.st.com/resource/en/application_note/an5354-getting-started-with-16bit-adc-on-stm32h7-mcus-stmicroelectronics.pdf), schematic và §6.6.

### Thấp

**R2-10 — §5.4–6.3, §7.2, §9.3, §10.1–10.2: các câu và tham chiếu không thống nhất ở mục 3.**

- **Rủi ro:** hai người triển khai chọn hai hợp đồng khác nhau.
- **Sửa:** đồng bộ ownership, startup, timeout, đơn vị KiB/byte và overhead CMUX; sửa follow-up theo ADC hiện tại.

Riêng lệnh **`AT+CMUX=0,0,5,2048,20,3,30,10,2`**: cách giải nghĩa speed và các timer trong bảng mới **đúng theo 27.007 §5.7**. Tôi không có bằng chứng HIL xác nhận mọi firmware EC600U hỗ trợ toàn bộ bộ tham số; việc nhóm vận hành đang dùng lệnh là bối cảnh do dự án cung cấp. [3GPP TS27.007](https://www.etsi.org/deliver/etsi_ts/127000_127099/127007/19.06.00_60/ts_127007v190600p.pdf).

## 5. Quyết định phê duyệt

**NOT APPROVED.** Các lỗi chặn của bản spec này là:

1. **R2-01:** OTA chưa có chuyển tiếp và trạng thái bền vững đầy đủ cho các đường phục hồi thất bại.
2. **R2-02:** session/QoS của MQTT 3.1 chưa được đặc tả đúng.
3. **R2-03:** ADC chưa bảo đảm cửa sổ dữ liệu nhất quán và phục hồi đồng bộ hai kênh.
4. **R2-04:** CMUX có quy tắc flow control sai và thiếu hợp đồng xử lý byte stream PPP.
5. **R2-05:** giới hạn body mới cho phép lưu cấu hình không thể gửi lại từ kết quả GET.

Cần sửa các hợp đồng trên và bổ sung các ca kiểm chứng tương ứng vào spec trước khi chuyển bản này sang lập kế hoạch triển khai. **Không cần mở lại D1–D12; yêu cầu oversampling phần cứng và DMA cho ADC được giữ nguyên.**