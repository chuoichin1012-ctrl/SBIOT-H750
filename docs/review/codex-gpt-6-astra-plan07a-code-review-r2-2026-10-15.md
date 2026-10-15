NOT APPROVED

Còn **3 lỗi Important**:

1. **Resync vẫn có thể kết thúc trước t3.5** — [serial.c:300](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/serial.c:300). Sau 200 ms, driver chuyển về timeout transparent. Ví dụ 1200 8E1, `packetInterval=1`: đuôi lỗi kéo dài quá 200 ms rồi nghỉ 5 ms sẽ xóa `resync`, dù chưa đủ t3.5 ≈ 32 ms; phần đuôi tiếp theo được phát cho subscriber. Cần giữ cơ chế chờ t3.5 độc lập với việc caller đã trả lỗi. Thêm test trường hợp này.

2. **Byte lỗi vẫn có thể được phát trước ERROR** — [uart_drv.c:170](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/uart_drv.c:170), [serial.c:176](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/serial.c:176). DMA HT/TC có thể đăng DATA trước USART đăng ERROR; `packetLength=1` sẽ phát byte lỗi. Ngoài ra, nếu ISR bị trễ, byte lỗi không nhất thiết là `wr−1`. Cần ngăn phát vùng dữ liệu nghi lỗi giữa cả hai nguồn ngắt, không chỉ đổi thứ tự trong USART ISR. Test lỗi đúng biên HT/TC và ERROR được xử lý trễ.

3. **Reset đã lên lịch vẫn cắt ngang wipe** — [flash_spi4.c:107](/home/linh/projects/SBIOT-Gateway/stm32/firmware/app/flash_spi4.c:107). OTA hoàn tất ngay trước `Z WIPE` đã hẹn reset sau 1,5 giây, trong khi wipe mất khoảng 22 giây. Seal chặn ghi mới nhưng không chặn reset; staging có thể còn nguyên và được cài khi boot lại. Cần hoãn reset trong lúc wipe hoặc từ chối bắt đầu wipe khi reset đang chờ; kiểm thêm ca OTA hoàn tất → `Z WIPE`.

Binary `test_serial_frame` hiện có chạy đạt; tôi chưa chạy lại board.