Plan 08c đã merge (PR #15). Đây là **Plan 09 (MQTT)**: `stm32/docs/plans/2026-10-16-09-mqtt.md` (branch `plan09-mqtt`).

Plan port `mqttgw` của ESP32 lên coreMQTT 2.3.1 (vendor, kèm bản vá MQTT 3.1), gồm:
- adapter transport trên `conn_t`;
- outbox giữ record tại chỗ, mỗi tin một trạng thái, nằm trong SRAM2+3;
- task `mqtt` là chủ duy nhất của coreMQTT; task `mqtt_io` chuyển dữ liệu giữa serial, outbox và Edge;
- channel 0 của Edge (report và truy cập từ xa qua MQTT).

**Đã kiểm chứng trong mã nguồn coreMQTT:**
- `sendBuffer` và `recvExact` gọi lại transport liên tục không nghỉ khi transport trả 0. Vì vậy adapter phải chờ trong `lwip_select` trước khi trả 0.
- PUBLISH đến lớn hơn buffer bị bỏ và trả `MQTTNoDataAvailable`, kết nối vẫn giữ.
- `MQTT_Publish` với DUP chấp nhận va chạm trạng thái (state collision).
- Payload được gửi bằng I/O vector, không chép vào network buffer.
- `handleSessionResumption` xoá các record khi là phiên mới.

**Xin duyệt plan, đặc biệt 10 ruling:**
1. Vị trí và nội dung bản vá 3.1.
2. Transport không bao giờ trả 0 mà không chờ trước; các timeout của coreMQTT đặt tường minh.
3. Outbox giữ record tại chỗ: gửi từ chính record mà không giữ mutex; tối đa 16 tin đang dở; phiên mới thì đưa tin về `QUEUED` sau `MQTT_Connect`; hết hạn tính theo uptime.
4. Client ID khi để trống thì suy từ MAC; ID dài quá với 3.1 thì broker trả `REFUSED`.
5. TLS: `sniHost` làm tên host; `sni` false nghĩa là kiểm theo `host`; chứng chỉ đọc lại ở mỗi lần connect, còn trạng thái thiếu chứng chỉ xác định lúc boot.
6. IO control chỉ Modbus RTU, đúng như code ESP32 (tài liệu ESP32 có nhắc JSON nhưng code không làm).
7. `reconnectIfSilent` chỉ tính PUBLISH đến và lúc connect.
8. Hai cổng: bitmask `ports`; ring lên mỗi cổng một MessageBuffer 6 KB và ring xuống 12 KB, đều trong SRAM4.
9. Channel 0 của Edge, và thứ tự khởi động `mqttgw` trước `edge_start`.
10. Các giá trị của `/api/status` `mqtt.state`.

Hãy đối chiếu với:
- spec §5.3, §6.1, §6.4, §7.3, §8 và §10.4;
- ESP32 `firmware/components/services/mqttgw.c` và `firmware/test/qemu/test_mqtt.py`;
- STM32 `components/services/*` (`bridge`, `tlsconn`, `edge_rt`).

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa. Nhớ là người dùng muốn giữ đơn giản.
