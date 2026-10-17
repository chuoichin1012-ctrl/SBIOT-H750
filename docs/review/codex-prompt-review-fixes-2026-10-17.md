Branch `review-fixes`, từ `main` (e2cca7e) tới HEAD, ở worktree `/home/linh/projects/SBIOT-Gateway/.claude/worktrees/review-fixes` (thư mục gốc của repo chính đang ở branch khác: hãy đọc file trong worktree này). Branch sửa các lỗi của một code review bên ngoài (6 lỗi hành vi, 2 lỗi standards), kèm một thay đổi form của web UI. Đây là bước review code bắt buộc trước PR.

**Các commit:**
- **b524500, form web UI:**
  - trường bắt buộc có dấu `*` (`isRequired`: `required`, number, ipv4) và `aria-required`;
  - select có `placeholder` ("Choose…"), không chọn lại được (`disabled hidden`); select `required` chỉ nhận giá trị nằm trong options (điểm đã đổi tên thì báo lỗi);
  - `EditDialog` không tô đỏ trường trống chưa chạm; Done hiện mọi lỗi và focus trường sai đầu tiên;
  - thêm `required` cho tên event/device/point, `di`, `formula` (virtual), các topic của Report, `template`, `willTopic`, `customModePorts`. Trước đó `fieldError()` trả về sớm với chuỗi rỗng nên `nameError` và `topicError` không chạy: **đây cũng là lỗi review P2-3** (tên rỗng lọt qua, bộ nạp từ chối, Edge không chạy).
- **391c244, P2-6 factory reset:** `fal_nor.c` gọi `store_port_progress()` sau mỗi sector xóa thành công; trên board là `wdog_hb_beat_self()`. Chờ lock của store vẫn không báo heartbeat. `test_store` kiểm: số lần progress bằng số sector reset xóa (160).
- **f39cac6, P1-1 và P2-5:**
  - `serial_write` trả `SERIAL_BUSY` (-3) khi không lấy được mutex của bus; `downlink()` beat `mqtt_io` sau mỗi lần forward lấy được bus và sau handler request của Edge. Một forward tối đa khoảng 177 s (tính trong comment), dưới timeout 400 s;
  - `wait_idle_bus()` có deadline (idle time + timeout của lần ghi) và trả false; `send_raw()` khi đó không gửi gì, trả -1. Text trên trang System được cập nhật.
- **b6c97ca, P2-2 và P2-4:**
  - kiểm thiết bị và point chuyển sang `src/lib/edgeModel.ts`. Trường địa chỉ của thiết bị không map từ chối slave ID mà một thiết bị map hay thiết bị ảo đang trả lời (spec §9.4, kiểm tra strict 3); hai thiết bị không map vẫn được trùng địa chỉ;
  - `EdgePage` chạy `modelErrors()` trên toàn mô hình trước mỗi lần Save (khi Edge bật). Có lỗi thì Save bị khóa, tab hiện "(check)", tab Data acquisition liệt kê lỗi. Save chờ đọc xong địa chỉ Modbus của trang IO;
  - `MAX_DEVICES = 64` áp dụng ở Add device, import CSV và Save;
  - `tests/unit/edgeModel.test.ts` kiểm các ca của review.
- **6f986ce, standards P2-1:** `audit_keys.py` lấy đường dẫn từ `Path(__file__)`.
- **a85881a, standards P3-2:** các bước kiểm binding ở boot trong spec theo v2 (104 byte, `SBB2`, `SBBD` dừng, CRC byte 0–99, UID và chữ ký).

**Kết quả:**
- **Web:** `npm run build`, `test:unit` 21/21, `test:e2e` (port-switch) đều xanh; probe Chromium trên bản mock cho thấy form Linkage và kiểm slave ID chạy đúng.
- **Host:** `test_store`, `test_store_small` xanh; check progress fail khi bỏ lời gọi (0 trên 160). Store mutants: kết quả giống `main` (mutant "failed save leaves its records" SURVIVED và "no re-validation at boot" không tìm thấy đoạn gốc, cả hai đều có sẵn trên `main`).
- **Firmware:** `boot` và `app` build sạch từ thư mục build mới; `check_elf` đạt. `audit_keys.py` chạy được từ gốc repo, `stm32/` và `/tmp`: 192 key, 0 key không được đọc.
- **Chưa thử trên board:** `mqttgw.c` và `serial.c` không có host test. Board bench đang được một luồng khác dùng.

**Phạm vi:** `git diff main HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi). Người dùng muốn giữ đơn giản: UI kiểm cấu hình, thiết bị chạy theo cấu hình được giao, không thêm cơ chế xử lý lỗi cấu hình trong firmware.
