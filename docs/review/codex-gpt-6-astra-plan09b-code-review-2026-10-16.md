NOT APPROVED

Còn 4 lỗi **Important**:

1. **Hook đếm lỗi cấp phát được cài quá muộn.** [cfgmodel.c:654](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/cfgmodel/cfgmodel.c:654) dựa vào `json_alloc_failures()`, nhưng lúc boot, network/system/io/serial/MQTT được nạp trước `json_alloc_install()`. Một lần cấp phát thất bại có thể trả config thiếu key thay vì NULL; thiếu username/password có thể làm thông tin đăng nhập runtime sai. **Sửa:** cài hook trước mọi lần nạp cấu hình; kiểm OOM qua đúng đường khởi tạo này.

2. **UI kiểm độ dài ký tự, firmware giới hạn byte.** [validate.ts:77](/home/linh/projects/SBIOT-Gateway/stm32/web/src/lib/validate.ts:77). Đã tái hiện: username `é` lặp 9 lần, dài 18 byte, vẫn hợp lệ trên UI; [web.c:628](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/web.c:628) chỉ giữ 16 byte, khiến đăng nhập bằng giá trị vừa lưu thất bại sau reboot. **Sửa:** kiểm số byte UTF-8 của chuỗi thực sự gửi cho các trường có giới hạn buffer.

3. **Kiểm xung đột listen port bị bỏ qua khi tải cấu hình phụ thuộc chậm hoặc lỗi.** [Port.tsx:175](/home/linh/projects/SBIOT-Gateway/stm32/web/src/pages/Port.tsx:175), tương tự [System.tsx:69](/home/linh/projects/SBIOT-Gateway/stm32/web/src/pages/System.tsx:69). Request lỗi bị đổi thành `null`, còn Save vẫn hoạt động; người dùng có thể lưu cổng trùng mà không thấy lỗi. **Sửa:** chỉ cho Save khi đã tải đủ cấu hình dùng để kiểm chéo; báo lỗi tải và cho thử lại.

4. **Import CSV Edge không kiểm đầy đủ giá trị node.** [edge.ts:349](/home/linh/projects/SBIOT-Gateway/stm32/web/src/lib/edge.ts:349). Đã tái hiện CSV có register `70000` được import với `errors=[]`; Save vẫn gửi, nhưng bộ nạp Edge từ chối sau reboot. **Sửa:** áp dụng các kiểm tra device/node hiện có cho dữ liệu CSV trước khi chấp nhận.

`test_cfgmodel` và `test_serial_frame` chạy đạt. Hai trường hợp UI trên được tái hiện trực tiếp bằng mã TypeScript; tôi không chạy lại bench board. Các đề nghị này giữ nguyên quyết định không validate nghiệp vụ ở đường lưu firmware.