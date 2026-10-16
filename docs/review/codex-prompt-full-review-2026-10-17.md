Hãy review toàn bộ dự án STM32 trong `stm32/` ở `main` (HEAD), trước khi sang Plan 11. Đây không phải review một diff. Hãy đọc code hiện tại như một người review độc lập, và tìm các lỗi sẽ ảnh hưởng tới người dùng thiết bị ngoài hiện trường.

**Dự án:**
- Gateway SBIOT-H750 trên STM32H750VB: FreeRTOS, lwIP (Ethernet IP101 và LTE EC600G-EU qua CMUX/PPP), Mongoose (web và WebSocket), mbedTLS 3, FlashDB trên SPI NOR, ứng dụng chạy trên QSPI.
- Spec: `stm32/docs/specs/2026-10-14-stm32-firmware-design.md`.
- Roadmap và các plan đã làm (01–10b): `stm32/docs/plans/`. Các ruling nằm trong từng plan.

**Phạm vi:**
- `stm32/firmware/boot`: bootloader, cài OTA, kiểm anti-clone;
- `stm32/firmware/app`;
- `stm32/firmware/components/*`: board, cfgmodel, store, netmgr (eth, LTE, CMUX, PPP, route), services (bridge, MQTT, TLS, Edge), serial, iomgr, sysmgr, webapi, syslog, ota, proto, arena;
- `stm32/firmware/tools` (mkimage, provision, embed_web);
- `stm32/web` (Preact UI);
- các test host và script bench, xem như bằng chứng.

Không review `third_party/` và `components/webapi/www/`, vì đây là file sinh ra.

**Các ràng buộc người dùng đã chốt.** Đừng báo những điều này là lỗi:
- **Giữ đơn giản.** Config chỉ đọc một lần lúc boot; lưu xong phải restart mới áp dụng.
- UI validate config. Gateway chỉ kiểm auth, tên, kích thước, cú pháp JSON và lỗi ghi. Thiết bị chạy với đúng config được đưa vào: không có chế độ cứu hộ, không có pending/rollback. Bộ nạp config chỉ lo an toàn bộ nhớ.
- **Lưu trữ:** phải sống sót qua mất điện và lỗi thoáng qua. Lỗi phần cứng kéo dài thì chuyển sang chỉ đọc, không cố sửa thêm.
- **Bootloader:** chỉ cài image và kiểm. Muốn về firmware gốc thì người dùng bấm Reload, không có cứu hộ tự động. Anti-clone: thẻ Ed25519 được ghi lúc nạp; sai thẻ thì xóa QSPI rồi dừng.
- Không có SNMP, Cloud, USB.
- **Những chỗ chưa kiểm, đã ghi lại:**
  - PPP sau khi phục hồi, vì SIM và mạng đang mất dịch vụ;
  - tải hai chiều và các bài cần vòng nối RS485.

**Hãy tìm, theo thứ tự ưu tiên:**
1. Lỗi bộ nhớ: tràn bộ đệm, use-after-free, chỉ số chưa được chặn, chuỗi thiếu NUL, tràn stack task.
2. Lỗi đồng thời: task với ISR với DMA, lock của lwIP/tcpip, các buffer và cache dùng chung (D-cache với DMA, MPU), race với callback.
3. Mất dữ liệu hay hỏng trạng thái khi mất điện: FlashDB, OTA staging/install, manifest của Edge.
4. **Bảo mật:**
   - auth của web và WebSocket, upload file;
   - xác thực image OTA, anti-clone;
   - kiểm chứng chỉ TLS;
   - lộ thông tin bí mật qua `/api` hoặc log.
5. Sai khác giữa firmware và Web UI: schema, mặc định, giới hạn, tên trường.
6. Hành vi sai so với spec mà người dùng sẽ thấy.

**Không cần báo:** style, đặt tên, refactor, hay những đề xuất làm phức tạp thêm mà không sửa một lỗi cụ thể nào.

**Trả lời:**
- Danh sách lỗi, xếp theo mức: Critical, rồi Important, rồi Minor. Mỗi lỗi ghi `file:dòng`, kịch bản lỗi cụ thể (đầu vào hoặc trạng thái nào dẫn tới hậu quả gì), và cách sửa đơn giản nhất.
- Cuối cùng, một dòng tổng kết: có lỗi Critical/Important nào cần sửa trước Plan 11 không.

Hãy xác minh từng lỗi với code thật; đừng suy đoán.
