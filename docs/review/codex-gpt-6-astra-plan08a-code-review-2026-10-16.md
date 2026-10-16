**NOT APPROVED**

Có 4 lỗi **Important** cần sửa:

1. **`idleRestart` có thể reset khi đang truyền dữ liệu** — [services.c:40](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/services.c:40). Bridge có thể cập nhật `s_net_us` sau khi task idle lấy `now_us()` nhưng trước khi đọc `last_activity()`. Phép trừ unsigned bị tràn, gây restart ngay. Đã xác nhận thứ tự đọc trong ELF. **Sửa:** lấy timestamp hoạt động trước, rồi lấy giờ hiện tại; thêm test xen kẽ cập nhật.

2. **UDP rỗng có thể làm đói task IO** — [bridge.c:553](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:553). Datagram dài 0 làm thoát trước khi đặt `work=true`. Khi chúng đến liên tục, `select()` trả ngay và bridge bỏ qua `vTaskDelay(1)`. **Sửa:** tính datagram rỗng là công việc rồi bỏ nó; kiểm flood UDP rỗng cùng nhịp IO.

3. **TCP server bỏ đói client khi có hơn 8 kết nối** — [bridge.c:319](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:319). Mỗi lượt luôn bắt đầu từ client cuối, dừng sau 8 lần đọc. Nếu 8 client cuối liên tục có dữ liệu, các client đầu không được phục vụ. **Sửa:** luân phiên vị trí bắt đầu; test ít nhất 9 client.

4. **Short connection kẹt sau khoảng 24,85 ngày uptime** — [bridge.c:416](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/services/bridge.c:416). Sau đóng vì idle, `next_try=0`; phép so sánh signed coi mốc này nằm trong tương lai khi uptime thuộc khoảng 24,85–49,71 ngày. **Sửa:** biểu diễn riêng trạng thái không chờ, hoặc dùng thời gian 64 bit; test quanh ngưỡng này.

Đã chạy lại bốn binary host test hiện có: đều đạt. Chưa chạy lại bench trên board.