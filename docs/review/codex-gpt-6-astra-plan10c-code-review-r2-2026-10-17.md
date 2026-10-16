NOT APPROVED

**Important — [serial.c:266](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/serial.c:266): tính nhầm bus đang nhận thành đã im lặng.**

Code lấy `now` trước `rx_quiet_since()`. Khi DMA vừa nhận thêm byte hoặc USART đang BUSY, hàm này cập nhật `since` bằng một lần `now_us()` mới hơn. Nếu `since > now`, phép trừ unsigned `now - since` quay vòng thành số rất lớn; vòng chờ thoát và có thể phát request ngay giữa frame đang nhận, gây va chạm/mất dữ liệu.

**Sửa nhỏ nhất:** gọi `rx_quiet_since(p)` trước, rồi lấy `now_us()` bằng câu lệnh riêng; giữ nguyên kiểm deadline sau đó.

Phần sửa placeholder đã xử lý đúng hai trường hợp được báo.