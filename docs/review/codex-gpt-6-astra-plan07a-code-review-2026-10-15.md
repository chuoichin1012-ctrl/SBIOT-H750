NOT APPROVED

Không thấy lỗi Critical; còn **6 lỗi Important**:

1. **Không phát hiện DMA ghi đè khi queue tồn đọng** — [serial.c:131](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/serial.c:131). Reader ở 0, DMA đã ghi 3072 byte, nhưng queue lần lượt chứa vị trí 1024/2048/3072: mỗi lần drain chỉ thấy 1024 byte, nên đọc dữ liệu đã bị ghi đè mà không báo overrun. Cần đối chiếu vị trí DMA **hiện tại**, bỏ event lỗi thời và đồng bộ lại reader. Thêm test trì hoãn task RX qua một vòng ring trong khi ISR vẫn chạy.

2. **Transaction kết thúc làm mất resync** — [serial.c:275](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/serial.c:275). Sau vi phạm t1.5, caller chuyển ngay sang transparent, hủy timer và xóa `resync`; phần đuôi lỗi có thể thành frame cho subscriber. Phải tiếp tục bỏ dữ liệu đến đủ t3.5 im lặng, dù transaction đã trả lỗi.

3. **Giao dữ liệu trước khi xử lý lỗi đường truyền** — [serial.c:173](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/serial.c:173). Với `packetLength=1`, drain của event ERROR đã phát byte lỗi trước khi bỏ frame. ISR còn xếp GAP trước ERROR khi hai cờ cùng xuất hiện. Cần xử lý lỗi trước khi phát dữ liệu; test parity với `packetLength=1` và RTO đồng thời với lỗi.

4. **Nhận nhầm bus đang bận thành im lặng** — [serial.c:211](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/serial.c:211). `last_rx_us` chỉ cập nhật khi drain. Một frame đang nhận, chưa tới HT và chưa có RTO, vẫn khiến collision avoidance hoặc transaction thấy timestamp cũ rồi phát chen vào. Cần kiểm tiến độ DMA thực trong cửa sổ im lặng; thêm ca nhận liên tục dưới 1024 byte rồi yêu cầu phát.

5. **Deadline t3.5 cộng sai phần lượng tử hóa RTO** — [uart_drv.c:236](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/serial/uart_drv.c:236). Tại 1200 8E1, RTOR được làm tròn thành 17 bit, nhưng timer vẫn cộng 18.334 µs: tổng khoảng **32,501 ms**, thay vì 32,084 ms. Frame mới sau gap hợp lệ 32,3 ms có thể bị đánh dấu vi phạm. Cần tính phần còn lại theo thời gian RTO thực đã lập trình và kiểm sát biên. [RM0433](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

6. **`Z WIPE` chưa độc quyền với OTA** — [app_main.c:338](/home/linh/projects/SBIOT-Gateway/stm32/firmware/app/app_main.c:338). Khóa store không chặn OTA web ghi staging. Upload đồng thời có thể ghi lại vùng vừa xóa, rồi bootloader cài image đó sau reset. Cần chặn và kết thúc OTA đang chạy trước khi wipe. **Có thể giữ `Z` trên console UART7 phục vụ provisioning**, nhưng chuyển sang file bench đơn thuần không sửa được race này.

**Minor:** [serial_check.py:215](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/bench/serial_check.py:215) bỏ qua exit code của flood; cần kiểm tiến trình gửi thành công để tránh PASS khi thực tế không có tải LAN.

Binary `test_serial_frame` hiện có chạy đạt. Tôi chưa build lại hoặc chạy board; các lỗi trên được xác định bằng đối chiếu luồng mã.