Plan 08a đã merge (PR #13). Đây là **Plan 08b**: `stm32/docs/plans/2026-10-16-08b-tls.md` (branch `plan08b-tls`).

**Nội dung 08b:**
- **T0 (đã xong, commit c1dcb2e):** listen port của TCP server không được trùng với cổng kia hay với web (`cfg_listen_ok`, kiểm khi lưu port1, port2, system).
- **T1:** người dùng yêu cầu khi chưa có giờ thì giờ hiển thị bắt đầu từ 2020-01-01 00:00:00 cộng uptime. Đây **chỉ là phần hiển thị**: `/api/status` có thêm `clockSet`, và console cũng hiện giờ này. Mọi thứ phụ thuộc đồng hồ vẫn thấy "chưa đặt": TLS, timer IO, log, và `sys_time` của Edge.
- **T2:** vendor mbedTLS 3.6.7 LTS (SHA-256 đã đối chiếu với digest của release) và cấu hình `tls_config.h` theo §7.3. `tls_core` có host test chạy trên chính thư viện thật, gồm:
  - luật ngày khi chưa có giờ;
  - ánh xạ mã trả về;
  - `cert_check` (F20);
  - `gmtime_r`.

  Bignum core nằm trong ITCM.
- **T3:**
  - `tlsconn` chạy trên socket lwIP non-blocking: handshake chờ bằng `lwip_select` cho tới deadline, không quay vòng; `tls_send_some` và `tls_recv` mỗi lần gọi chỉ thử một lần; `tls_pending`.
  - `conn_t` trong `svc_net` định tuyến qua TLS.
  - Bridge: TCP client và HTTP client dùng TLS; `wait()` xử lý dữ liệu đã giải mã còn nằm trong mbedTLS.
  - Host test `test_tlsconn` chạy qua loopback với một server mbedTLS, gồm các ca: wrong CA, wrong name, expired, mutual, deadline, slow reader 256 KB, pending 12 KB, close.
- **T4:** kiểm chứng chỉ khi upload (F20): `subject`, `validTo`, `expired`, `keyType`, `pairMatches`. `restartNeeded` chỉ true cho mqtt.
- **T5:** bench `tls_check.py` với peer Python `ssl` phía Windows, gồm 10 ca. Ghi lại headroom stack, heap mỗi phiên và nhịp IO khi handshake liên tục thất bại.

Xin duyệt plan, đặc biệt 9 ruling trong mục "Rulings to approve":
1. Tên file config là `tls_config.h`: tên `"mbedtls_config.h"` sẽ include nhầm config mặc định của thư viện (đã thấy khi build thử).
2. Không bật `MBEDTLS_PLATFORM_MEMORY`, vì `calloc`/`free` vốn đã đi qua wrapper `heap_5`.
3. Không bật `MBEDTLS_THREADING_C`: mỗi phiên tự sở hữu mọi context, và `board_rng` đã có mutex.
4. Các hàm port lấy đúng theo `nm -u` của bản build thử.
5. Luôn gọi `set_hostname` (CVE-2025-27809, áp dụng từ 3.6.3).
6. `restartNeeded` chỉ true cho mqtt, giống ESP32.
7. Giờ 2020 chỉ để hiển thị; `sys_time` của Edge (08c) vẫn để trống khi chưa có giờ.
8. Bignum core nằm trong ITCM.
9. Chỉ vendor `library/`, `include/` và `LICENSE`.

Hãy đối chiếu với:
- spec §5.3, §5.5, §5.6, §6.1, §7.3, §10.2 và §10.4;
- ESP32 `firmware/components/services/tlsconn.c`, `firmware/components/webapi/api.c` (`cert_post`) và `firmware/test/qemu/test_bridge.py` (mục TLS);
- STM32 `components/services/{net.c,bridge.c}`, `components/webapi/web.c` (`upload_complete`) và `components/sysmgr`.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa. Nhớ là người dùng muốn giữ đơn giản.
