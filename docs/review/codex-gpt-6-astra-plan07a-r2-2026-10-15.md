NOT APPROVED

Còn **một điểm phải sửa ở bench XOFF/XON** — [plan:113](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-07a-serial.md:113):

PC gửi XOFF **giữa lúc gateway đang phát** trên RS485 hai dây sẽ gây tranh chấp bus; đây không phải phép kiểm flow control hợp lệ. Hai dây chỉ cho phép một driver phát tại một thời điểm. [Modbus Serial Line §3.3.2](https://www.modbus.org/docs/Modbus_over_serial_line_V1_02.pdf).

Không cần bổ sung thiết bị ngay:
- Trên RS485: gửi XOFF **trước** khi yêu cầu gateway phát, rồi kiểm XON, timeout và lần gửi tiếp theo.
- Kiểm dừng/resume **DMA đang chạy** bằng fault injection trên board; ghi rõ chưa kiểm đường nhận XOFF đồng thời với TX qua RS232.

Các sửa đổi RTU, tái đồng bộ, deadline/abort và diễn giải DE đã đáp ứng yêu cầu vòng trước.