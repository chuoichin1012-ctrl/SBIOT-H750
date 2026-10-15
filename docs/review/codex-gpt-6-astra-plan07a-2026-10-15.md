NOT APPROVED

Cần bổ sung 3 điểm trước khi code:

1. **Chốt cách phát hiện và tái đồng bộ RTU** — [plan:35](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-07a-serial.md:35). HT/TC không báo từng byte, nên phải nêu cách phát hiện ký tự trong cửa sổ t1.5–t3.5, kể cả ký tự bắt đầu trước nhưng hoàn tất sau deadline. Khi vi phạm, phải bỏ frame và chờ đủ t3.5 im lặng để đồng bộ lại; không kết thúc theo deadline cũ rồi nhận phần đuôi thành frame mới. Thêm test hai trường hợp này và nhận tiếp sau nhiều RTO liên tiếp như §6.5 yêu cầu. [Modbus §2.5.1.1](https://www.modbus.org/docs/Modbus_over_serial_line_V1_02.pdf).

2. **Chốt timeout/abort cho TX và XON/XOFF** — [plan:44](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-07a-serial.md:44). Cần nói rõ XOFF dừng DMA đang phát thế nào, không chỉ chặn chunk kế tiếp; khi không nhận XON hoặc DMA lỗi thì hết hạn phải dừng TX an toàn, trả lỗi và nhả mutex. Thêm test XOFF không có XON, lỗi DMA, rồi lần gửi tiếp theo vẫn hoạt động. Hiện plan chưa đủ để loại trừ việc giữ bus và task console vô hạn.

3. **Bench phải xác nhận khoảng nghỉ trên đường truyền** — [plan:97](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-07a-serial.md:97). Ghi rõ **1200 8E1**: t1.5 = 13,75 ms, t3.5 ≈ 32,08 ms. Khoảng nghỉ giữa hai lệnh `Write` của Windows không tự chứng minh khoảng nghỉ giữa các ký tự vì còn bộ đệm truyền. Cần timestamp phía board hoặc phép đo đường truyền xác nhận gap nằm giữa hai ngưỡng; không tính PASS chỉ vì transaction timeout. [Microsoft SerialPort](https://learn.microsoft.com/en-us/dotnet/api/system.io.ports.serialport?view=net-7.0).

Bốn ruling chấp nhận được về hướng thiết kế; RS232 vẫn ghi rõ **chưa kiểm chứng**. Tách `serial_frame`, dùng ISR LL riêng, TIM2 CC1/CC2 và buffer TX 2 KB đều hợp lý. Không cần thêm framework hay đổi kiến trúc.