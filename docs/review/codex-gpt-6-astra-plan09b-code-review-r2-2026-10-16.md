NOT APPROVED

Đã kiểm commit sửa code `07fed4d`. Hook cấp phát đã sửa đúng; còn 3 lỗi **Important**:

1. **Giới hạn byte vẫn kiểm chuỗi đã `trim()`.** [validate.ts:77](/home/linh/projects/SBIOT-Gateway/stm32/web/src/lib/validate.ts:77). Đã tái hiện: `'é'.repeat(8) + ' '` dài 17 byte nhưng được chấp nhận. UI gửi nguyên chuỗi; firmware cắt còn 16 byte, khiến thông tin đăng nhập sau reboot khác giá trị đã nhập. **Sửa:** kiểm byte của chuỗi nguyên bản; thêm test Unicode kèm khoảng trắng.

2. **Save vẫn có thể dùng cấu hình phụ thuộc cũ khi chuyển UART.** [Port.tsx:210](/home/linh/projects/SBIOT-Gateway/stm32/web/src/pages/Port.tsx:210). `useAsync` giữ `data` cũ khi tải lại hoặc lỗi. Đi từ UART1 sang UART2, nếu GET UART1 thất bại, Save vẫn mở và kiểm UART2 với bản UART2 cũ; có thể lưu cổng trùng UART1. **Sửa:** chặn Save khi đang tải hoặc có lỗi, bảo đảm dữ liệu phụ thuộc thuộc UART hiện tại.

3. **CSV vẫn lọt giá trị ngoài danh sách lựa chọn.** [Acquisition.tsx:178](/home/linh/projects/SBIOT-Gateway/stm32/web/src/pages/edge/Acquisition.tsx:178). CSV có function code `99`, kiểu `uint16`, register `0` vẫn qua `modelErrors`: `formErrors` không kiểm giá trị thuộc `options` của select. Sau lưu, bộ nạp Edge từ chối model. **Sửa:** kiểm membership của các trường lựa chọn khi import; thêm ca CSV function code ngoài `1–4`.