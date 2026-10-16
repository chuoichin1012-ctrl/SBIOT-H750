Đã review code hiện tại trên `main` tại **`6d69e2b`**, chỉ đọc. Các phát hiện dưới đây dựa trên đường chạy cụ thể trong code; chưa tái hiện trên board.

### Critical

**1. JSON lồng sâu làm tràn stack, từ request mạng hoặc template đã lưu.**  
Nguồn: [rwprot.c:35](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/proto/rwprot.c:35), [edgecore.c:360](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/proto/edgecore.c:360), [api.c:301](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/api.c:301) và `:314`.

Các đường này gọi cJSON trước khi kiểm độ sâu. JSON `{"x":` + 300 dấu `[` + `0` + 300 dấu `]` + `}` chỉ **607 byte**, vừa mọi giới hạn body liên quan. ELF hiện có dùng **48 byte stack mỗi tầng parse**, tức hơn 14 KB, trong khi `mqtt_io` chỉ có 4 KB, `init` 6 KB và `web` 8 KB. Request Edge JSON có thể làm hỏng stack; lưu nó làm template còn có thể gây lỗi mỗi lần boot.

**Sửa:** dùng scanner hiện có để chặn độ sâu trước cJSON tại các điểm nhận này; UI kiểm cùng giới hạn cho template.

### Important

**2. Cache lưu con trỏ tên cấu hình đã hết thời gian sống.**  
Nguồn: [store.c:159](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:159), [api.c:332](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/api.c:332).

Sau nâng cấp, việc bổ sung default có thể làm cache 20 KB đầy, khiến `load()` bỏ qua một cấu hình. Khi người dùng thu nhỏ cấu hình khác rồi lưu cấu hình bị bỏ qua, `cache_put()` giữ con trỏ tới `char name[16]` trên stack HTTP. Request sau tái sử dụng stack đó, khiến tên entry thay đổi: GET có thể trả nhầm cấu hình hoặc đọc bộ nhớ hết hiệu lực.

**Sửa:** cache tự giữ tên, hoặc dùng con trỏ tên chuẩn có lifetime tĩnh từ `cfgmodel`.

**3. `workMode` âm gây đọc ngoài mảng khi boot.**  
Nguồn: [bridge.c:932](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:932).

Lưu `"workMode":-1` rồi restart dẫn tới `MODES[-1]`; con trỏ ngoài mảng được đưa vào `%s`, có thể gây HardFault. Bộ nạp chỉ kiểm kiểu JSON nên đường này thực sự tới được.

**Sửa:** chặn chỉ số trước khi chọn tên mode; dùng `"UNKNOWN"` ngoài miền. Không cần bổ sung validate nghiệp vụ.

**4. Transaction serial có thể giữ mutex bus vô hạn.**  
Nguồn: [serial.c:261](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/serial.c:261).

Vòng chờ kết thúc dữ liệu đang nhận không xét `timeout_ms`. Với `packetInterval=100 ms` và dữ liệu đến liên tục hoặc cách nhau dưới 100 ms, transaction không bao giờ trả về. Polling bị kẹt; nếu caller là MQTT remote thì cả `mqtt_io` cũng ngừng xử lý.

**Sửa:** đặt deadline cho bước chờ này; hết hạn phải nhả bus và trả timeout.

**5. Một giá trị Modbus nhiều thanh ghi có thể bị ghép từ hai mẫu khác nhau.**  
Nguồn: [edge_rt.c:502](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/edge_rt.c:502), [modbus.c:172](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/proto/modbus.c:172).

Qua MQTT remote, mỗi word được đọc dưới một lần khóa riêng. Poll task hoặc `edge_ctl` có thể cập nhật giữa hai word. Ví dụ raw float32 đổi từ `3f80 0001` sang `4000 8000`, response có thể chứa `3f80 8000`, không thuộc mẫu nào.

**Sửa:** lấy snapshot các thanh ghi cần trả dưới một lần khóa cho FC3/4; không giữ khóa cache khi thực hiện ghi xuống bus.

**6. Upload OTA mới có thể phá image đã nhận thành công đang chờ restart.**  
Nguồn: [web.c:232](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/web.c:232), [web.c:294](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/web.c:294).

Sau khi trả 200 và lên lịch restart, `upload_end()` xóa `s_ota_busy`. POST thứ hai được phép xóa magic staging; restart cũ có thể xảy ra khi body thứ hai còn dở. Khi đó image thứ nhất đã báo thành công cũng không được cài. UART7 còn có uploader riêng, không chung quyền sở hữu staging.

**Sửa:** dùng quyền sở hữu staging chung; giữ khóa sau khi upload thành công cho tới restart.

**7. `rw_prot` rò heap khi dựng response gặp OOM.**  
Nguồn: [rwprot.c:59](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/proto/rwprot.c:59), `:70`, `:74`, `:86`.

Nếu tạo mảng trả lời thất bại, `list=NULL` nhưng vòng lặp vẫn tạo các object `x`. `cJSON_AddItemToArray(NULL,x)` thất bại mà không giải phóng chúng. Các request tiếp theo có thể tiếp tục làm mất heap.

**Sửa:** dừng và dọn sạch khi không tạo được cây/mảng; giải phóng item nếu thêm thất bại. Bổ sung fault injection cho handler này.

**8. NO SIM trên PPP thường gây vòng RESET_N.**  
Nguồn: [lte.c:612](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte.c:612), [lte.c:715](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte.c:715).

Khi CMUX bị từ chối và CPIN chưa READY, `plain_ppp()` trả `PLAIN_NO_SIM`. Caller vẫn chạy `recover()`, rồi lặp lại. Modem còn trả lời AT nhưng bị reset thay vì chờ hỏi CPIN mỗi 60 giây.

**Sửa:** xử lý riêng `PLAIN_NO_SIM`, giữ modem chạy và hỏi lại SIM; chỉ RECOVER khi modem im lặng.

**9. Client ngừng đọc có thể chiếm hết kết nối web vô hạn.**  
Nguồn: [web.c:567](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/web.c:567).

Tám client chưa đăng nhập GET file JS rồi ngừng đọc, nhưng TCP vẫn ACK, có thể giữ cả tám slot. Timeout hiện chỉ đóng `ST_IDLE` khi hàng gửi rỗng; response đang kẹt không có deadline. Các kết nối quản trị mới bị từ chối. GET Edge bị kẹt còn giữ pin generation.

**Sửa:** timeout theo thời gian không tiến triển của đường gửi; đóng kết nối và bỏ pin khi hết hạn.

**10. Placeholder hợp lệ trên UI có thể làm toàn bộ report sai JSON.**  
Nguồn: [edgecore.c:396](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/proto/edgecore.c:396).

Placeholder `1abc` hoặc `null-device` được cJSON nhận phần tiền tố là literal; code sau đó chép toàn bộ chuỗi không quote. Khi điểm đọc lỗi, report thành dạng `{"temperature":1abc}`, hệ thống nhận không parse được.

**Sửa:** chỉ coi là literal khi parser đọc hết chuỗi; nếu không thì quote thành chuỗi JSON.

### Minor

**11. UI cho phép web và WebSocket dùng cùng cổng.**  
Nguồn: [rules.ts:30](/home/linh/projects/SBIOT-Gateway/stm32/web/src/lib/rules.ts:30).

`webPort=80`, `websocketPort=80`, `websocketTarget=3` vượt qua kiểm tra UI. Sau reboot, hai listener tranh cùng cổng và một dịch vụ không lên.

**Sửa:** kiểm hai cổng khác nhau khi console bật, tại UI.

**12. Status hiển thị cấu hình đã lưu thay vì trạng thái đang chạy.**  
Nguồn: [api.c:130](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/api.c:130).

Lưu tắt MQTT/Edge nhưng chưa restart: task vẫn hoạt động, trong khi Status hiển thị “Disabled” vì `enabled()` đọc cache cấu hình mới.

**Sửa:** lấy cờ enabled từ runtime được chốt lúc boot.

**13. UI không cho lưu chế độ report chỉ khi thay đổi.**  
Nguồn: [Report.tsx:31](/home/linh/projects/SBIOT-Gateway/stm32/web/src/pages/edge/Report.tsx:31).

Dù điểm đã bật `onChange`, đặt `periodic=false` và `scheduled=false` vẫn làm Save bị khóa. Runtime hỗ trợ trường hợp này.

**Sửa:** bỏ điều kiện bắt buộc periodic/scheduled, hoặc cho phép khi có điểm `onChange`.

Các binary host liên quan chạy lại đều đạt; không build lại, không chạy board và đã tắt LeakSanitizer do giới hạn sandbox. Những kết quả đó không bao phủ các kịch bản lỗi trên.

**Tổng kết: Có — cần sửa các lỗi Critical/Important số 1–10 trước Plan 11.**