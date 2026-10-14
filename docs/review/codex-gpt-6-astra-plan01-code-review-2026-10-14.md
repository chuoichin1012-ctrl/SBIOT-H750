### Strengths

Đã review `5ba9501..045154d`, giữ nguyên working tree, index, HEAD và branch.

Năm mục Review Focus:

1. **`cfg_load`:** mã và test kiểm đủ việc bỏ `telnet`/`telnetPort`/`rfc2217`, giữ các key hợp lệ.
2. **Hai cổng:** có test chấp nhận `websocketTarget=3`, `edgeReport.channel=2`, `uart=2`, `AI2`/`DI2`.
3. **TLS UART2:** điều kiện hiển thị và `scope=port2` đúng; còn lỗi giữ state giữa hai cổng bên dưới.
4. **100 điểm:** UI dùng chung `MAX_NODES=100`; backend có test biên 100/101.
5. **ESP32:** snapshot **359 file khớp**, không có file thêm/xóa trong phạm vi snapshot.

`proto` được chép đúng phạm vi; cJSON và LICENSE khớp nguồn. `www` được ignore và không nằm trong index. Các `Ruling:` về checkout, `.gitignore`, browser headless và cổng dev server hợp lý.

**Đã kiểm đạt:** schema check, TypeScript, kiểm cú pháp C và build bundle firmware UI trong RAM. **Chưa chạy lại host test runtime:** compiler bị sandbox chỉ đọc chặn tạo file tạm.

### Issues

#### Critical (Must Fix)

Không phát hiện.

#### Important (Should Fix)

**I1 — Có thể lưu cấu hình UART1 vào UART2 khi chuyển trang.**  
[hooks.ts:65](/home/linh/projects/SBIOT-Gateway/stm32/web/src/lib/hooks.ts:65), [Port.tsx:203](/home/linh/projects/SBIOT-Gateway/stm32/web/src/pages/Port.tsx:203)

`useForm` không reset `ready` và dữ liệu khi `n` đổi; nút Save lập tức sử dụng `port${n}` mới. Đã tái hiện với hook thật: sau khi chuyển sang UART2, cả lúc đang tải lẫn khi GET thất bại, form vẫn `ready=true`, `valid=true`, giữ baud rate UART1. Người dùng có thể ghi đè cấu hình cổng khác; lỗi tải cũng không hiện qua `Pending`.

**Sửa:** cô lập form theo UART, chẳng hạn component con có `key={n}`; chỉ cho lưu khi dữ liệu của đúng cổng đã tải thành công. Thêm test chuyển cổng với GET chậm và GET lỗi.

**I2 — File và kết quả upload chứng chỉ có thể đi theo sang cổng khác.**  
[CertUpload.tsx:20](/home/linh/projects/SBIOT-Gateway/stm32/web/src/components/CertUpload.tsx:20), [Port.tsx:196](/home/linh/projects/SBIOT-Gateway/stm32/web/src/pages/Port.tsx:196)

Các hàng chứng chỉ chỉ có key theo `kind`, còn `file` và `info` nằm trong state. Khi hai cổng đều bật TLS cùng chế độ xác minh, đổi `scope` không remount hàng: file đã chọn và kết quả upload của cổng trước có thể hiện ở cổng sau. Đây là **suy luận từ vòng đời component**, chưa tái hiện bằng browser trong phiên này.

**Sửa:** remount subtree theo cổng hoặc key theo `scope` và `kind`; kiểm chuyển cổng không giữ file/kết quả upload cũ. Cách cô lập toàn bộ form ở I1 có thể xử lý đồng thời.

#### Minor (Nice to Have)

**M1 — Host Makefile thiếu dependency của header.**  
[Makefile:11](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/host/Makefile:11)

Sửa header có thể vẫn chạy binary cũ. Đã kiểm bằng `make -n -W ../../components/proto/include/modbus.h build/test_proto`: kết quả vẫn “up to date”. Thêm dependency header hoặc sinh/include file `.d`; tránh đưa header vào danh sách source compile qua `$^`.

**M2 — Mock STM32 vẫn báo PSRAM của ESP32.**  
[mock.ts:92](/home/linh/projects/SBIOT-Gateway/stm32/web/src/lib/mock.ts:92)

`psramFree: 3900000` khiến trang Status hiển thị bộ nhớ không có trong thiết kế STM32. Đặt bằng 0 hoặc bỏ trường khi hợp đồng status cho phép.

### Declined to judge

- ADC, DMA/cache, XIP, watchdog và timing modem: thuộc các sub-plan có board.
- HTTP backend, lưu flash, MQTT/LTE và xác minh chứng chỉ thực: Plan 01 chưa triển khai các dịch vụ này.
- Giới hạn body, arena và ngân sách RAM trên MCU: đã phân bổ cho các plan sau; host core hiện tại chưa phải nghiệm thu target.
- Audit toàn diện thuật toán và thư viện được chép nguyên: không coi việc chép khớp là bằng chứng đã kiểm hết mọi hành vi nền.

### Assessment

**Ready to merge? With fixes.** Cần sửa I1–I2 và kiểm hồi quy chuyển UART trước merge. Phần triển khai theo plan nhìn chung đúng, nhưng việc mở cổng thứ hai đã làm lộ lỗi cô lập state mà các kiểm tra từng trang riêng lẻ chưa bao phủ.