Plan 08b đã merge (PR #14). Đây là **Plan 08c**: `stm32/docs/plans/2026-10-16-08c-edge.md` (branch `plan08c-edge`). Plan port Edge runtime của ESP32 (`firmware/components/services/edge_rt.c`):
- polling: RTU và DL/T645 trên cả hai cổng, ≤ 5 thiết bị Modbus TCP, IO, điểm ảo;
- report: theo chu kỳ, theo lịch, khi thay đổi;
- truy cập từ xa qua socket của cổng;
- linkage.

Cấu trúc:
- nạp mô hình theo từng node, qua visitor mới `cfg_edge_load`;
- logic thuần nằm trong `edge_core`, có host test;
- phần chạy trên board nằm trong `edge_rt.c`;
- bridge có thêm slot report và hook Edge.

Xin duyệt plan, đặc biệt 6 ruling:
1. **Report đi qua slot riêng của bridge, không qua MessageBuffer.**
   - `xMessageBufferReceive` để nguyên message lớn hơn buffer ở đầu hàng đợi và trả 0, nên một report lớn hơn 1,5 KB sẽ chặn hàng đợi của cổng mãi mãi.
   - Report không kèm registration prefix.
   - `bufferOffline` giữ frame serial, không giữ report.
2. **Bộ nhớ của Edge cấp từ heap lúc boot, theo đúng kích thước cấu hình:** mô hình, template, buffer report, stack của các task Edge. AXI và DTCM không còn đủ chỗ cho phương án tĩnh ở §5.3/§6.1. Cập nhật §5.3 và §6.1.
3. **Truy cập từ xa trên cổng của `channel`, đặt trước các đường xử lý của 08a.**
   - Edge chỉ trả lời các unit nó expose; phần còn lại đi tiếp đường 08a như cũ.
   - Framing MBAP dùng `acc` của client; framing RTU dùng `rtu_push` mới.
   - Ở chế độ HTTP client không có truy cập từ xa.
4. **`channel` 0 (MQTT) trước Plan 09:** Edge vẫn chạy, nhưng chưa gửi report và chưa có truy cập từ xa qua MQTT; trạng thái và log ghi rõ.
5. **Giờ:**
   - lịch report dùng `sysmgr`/`clock.c` theo timezone;
   - `sys_time` là placeholder khi chưa có giờ (ruling 7 của 08b).
6. **Nạp lúc boot:**
   - dùng buffer văn bản Edge của web, bàn giao trước `webapi_start`;
   - `cfg_edge_load` dùng chung code với `cfg_edge_text` mà không đổi một byte nào của văn bản `cfg_edge_text` xuất ra.

Hãy đối chiếu với:
- spec §4, §5.3, §6.1, §6.3, §6.4, §9.3, §9.4 và §10.4;
- ESP32 `edge_rt.c`, `bridge.c` (`bridge_send`, `bridge_set_handler`) và `test/qemu/test_edge.py`;
- STM32 `components/services/bridge.c`, `cfgmodel.c` (`cfg_edge_text`) và `store` (`store_edge_text`).

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa. Nhớ là người dùng muốn giữ đơn giản.
