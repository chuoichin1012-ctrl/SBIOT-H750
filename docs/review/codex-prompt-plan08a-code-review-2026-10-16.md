Plan 08a (đã duyệt r4) đã code xong và chạy trên board. Branch `plan08a-bridge`. Trước khi tạo PR, xin **review code**: `git diff main..HEAD -- stm32/` cùng plan `stm32/docs/plans/2026-10-16-08a-bridge.md` (Bench findings N1–N8, Measurements).

**Trọng tâm:**
1. `components/services/bridge_core.c` (`test_bridge_core`):
   - config, `mbtcp_push` (đồng bộ lại bằng cách bỏ từng byte, khác ESP32);
   - HTTP head/tail/body;
   - `tx_progress` (mỗi lần gọi chỉ thử gửi một lần, quy tắc 10 s không tiến triển), `net_classify`, `accept_allowed`.
2. `components/services/net.c`:
   - socket non-blocking, chỉ dùng tên lwIP (`closesocket`, `lwip_fcntl`);
   - connect có timeout, `conn_send_all` cho các câu trả lời nhỏ.
3. `components/services/bridge.c`, một instance cho mỗi cổng:
   - MessageBuffer trong SRAM2+3, một bên ghi (subscriber), một bên đọc (`bridge[n]`);
   - `send_frames` với offset riêng cho từng kết nối; giới hạn việc mỗi lượt và `vTaskDelay(1)`;
   - TCP server (ACL, giới hạn client, gate netconn), TCP client (registration, heartbeat, short connection, Modbus TCP client), UDP (giữ datagram khi `NET_AGAIN`), multicast, HTTP;
   - Modbus gateway: IO slave từ map, `serial_transact` trên đúng cổng; polling.
4. `services.c`: `idleRestart`, `svc_net_activity`.
5. `iomgr/io_mb.c` (`test_io_mb`), `sysmgr_payload`.
6. **N1 (`app/arch/cc.h`):** bỏ `LWIP_PROVIDE_ERRNO 0`, vì lwIP kiểm macro này bằng `#ifdef`. Trước đây lwIP dùng mã lỗi riêng và biến errno toàn cục của newlib, còn bên gọi đọc `__errno()`. Thay đổi này ảnh hưởng cả Mongoose (`web_check` vẫn 44/44).
7. **N2:** cfgmodel cho `port2` nghe mặc định ở 24.
8. **Bộ nhớ:** `check_elf.py` (hàng đợi trong SRAM2+3, stack bridge trong AXI, không link `close`/`fcntl`); spec §5.3 và §5.6 đã cập nhật.

**Ledger rulings** (tóm tắt):
- `mbtcp` đồng bộ lại theo từng byte;
- `tx_progress` mỗi lần gọi một lần thử;
- `sysmgr_payload` không có host test (phần custom đi qua `sf_payload_custom`);
- trạng thái bridge để trong SRAM2+3;
- câu trả lời cho một request được gửi trong ≤ 2 s, quá hạn thì đóng (như ESP32);
- `port2` mặc định 24;
- `idleRestart` kiểm ở 60 s;
- ca tải chỉ đọc phía PC (bus bán song công);
- ca client kẹt đo đúng hành vi đã thiết kế.

**Kết quả:** host test `test_bridge_core`, `test_io_mb`, `test_cfgmodel`, `test_serial_frame` GREEN. Trên board:
- `bridge_check.py` 68/68 (peer chạy phía Windows);
- `serial_check` 35/35, `web_check` 44/44, `store_check`, `io_check adcbase` và `dido` (thêm ca lọc DI 1000 ms) đạt;
- `check_elf` đạt.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa (lỗi thật, theo mức độ). Nhớ người dùng muốn giữ đơn giản.
