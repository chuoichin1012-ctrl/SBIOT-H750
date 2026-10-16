Plan 07b đã merge (PR #12). Đây là **Plan 08a**: `stm32/docs/plans/2026-10-16-08a-bridge.md` (branch `plan08a-bridge`).

Roadmap row 08 (Data services) quá lớn nên đề xuất chia ba:
- **08a** (plan này): bridge theo từng cổng, không TLS;
- **08b**: mbedTLS 3.6 LTS và `tlsconn` (§7.3), TLS cho TCP client và HTTPS, kiểm chứng chỉ khi upload;
- **08c**: `edge_rt` theo bus, thiết bị Modbus TCP, buffer report ≤ 26 KB, truy cập Edge từ xa qua bridge.

**Nội dung 08a:** port `bridge.c`, `net.c`, `services.c` của ESP32 thành một instance cho mỗi cổng:
- các chế độ UDP/TCP client/server, HTTP client, Modbus TCP gateway (server và client), short connection, multicast;
- polling, registration, heartbeat mạng;
- IO slave (`io_mb.c`, chuyển từ 07b) trả lời trên cả hai cổng;
- `bufferOffline`, `idleRestart`.

Logic thuần có host test: `bridge_core` (config, ghép lại Modbus TCP, HTTP), `io_mb`, `sysmgr_payload`. Bench: bản port `test_bridge.py` của ESP32 (bỏ RFC 2217, PUSR, TLS), peer chạy phía Windows.

Xin duyệt plan, đặc biệt 5 ruling:
1. Chia 08a/08b/08c.
2. Dùng MessageBuffer thay ring buffer của ESP32 (tĩnh trong SRAM2/3, mỗi buffer một bên ghi). `bridge_send()` để tới khi có bên dùng đầu tiên.
3. Chế độ TLS bị từ chối cho tới 08b, có trạng thái rõ ràng.
4. Peer của bench chạy phía Windows (WSL nằm sau NAT); UART2 là phía thiết bị.
5. Multicast chỉ kiểm việc join và nhận từ một máy gửi Windows.

Đối chiếu spec §4, §5.3, §5.6, §6.1, §6.3, §6.4, §13 và ESP32 `firmware/components/services/{bridge,net,services}.c`, `firmware/test/qemu/test_bridge.py`.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa. Nhớ người dùng muốn giữ đơn giản.
