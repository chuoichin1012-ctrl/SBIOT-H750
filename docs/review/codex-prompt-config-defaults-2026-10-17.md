Review code trước PR cho branch `config-defaults`. Branch nằm trong worktree `.worktrees/cfg`. Phạm vi: `git diff main config-defaults -- stm32`. Các yêu cầu đều do người dùng đưa ra trực tiếp trong ngày 2026-10-17, không có plan riêng.

**1. Config:**
- **GET không bao giờ thiếu key:** `store.c` `with_defaults()`. Ở `load()` và `store_set_text()`, một config dạng object mà bản lưu thiếu key nào thì GET điền key đó từ mặc định của `cfgmodel`. Flash vẫn giữ nguyên bytes đã gửi. Lý do: một bản `network` cũ chỉ có 8 key, nên ô Authentication của UI nhận null. Thay đổi này sửa ruling 4 của Plan 09b. Body đầy đủ vẫn đọc lại đúng từng byte.
- **Mặc định không còn chuỗi rỗng, và có dữ liệu mẫu:**
  - broker `broker.emqx.io`, Client ID `SBIOT-GW`, topic `sbiot/gw/...`, Last will điền sẵn nhưng tắt;
  - một topic publish và một topic subscribe;
  - một device Modbus trên UART1 với hai point;
  - một linkage.

  Tất cả đều tắt. Spec §9 đã ghi. Cache của bản test nhỏ tăng lên 4800 byte; tổng các mặc định là 4555 byte.
- **SN:** `netcfg_serial()` cho `SB` cộng 5 byte cuối của MAC, 12 ký tự, thay cho 24 chữ số hex của UID.

**2. Web UI:**
- layout gồm header (logo, tên, nút menu trên mobile, Sign out), sidebar và main content, bảng màu SBIOT sáng;
- trang Status: OS là "FreeRTOS", RAM trống, tín hiệu hiện bằng cột sóng;
- tab có vạch cam; console Websocket nền sáng; thanh Save nằm ở đáy màn hình;
- khi bấm HEX/ASCII, ô dữ liệu đi kèm tự chuyển đổi (`convertData`);
- mục mới khớp với `ITEM_*`, tên tự đánh số; mock theo đúng các mặc định của firmware.

**Kết quả:**
- **Host:** `test_cfgmodel`, `test_store`, `test_store_small`, `test_netcfg` xanh; web unit test 11/11; tsc đạt.
- **Board:**
  - `defaults_check`: bản lưu thiếu key được điền đủ; sau factory reset không còn trường null hay rỗng; dữ liệu mẫu có đủ và đều tắt;
  - `pages.mjs` đạt (lần chạy đầu ngay sau factory reset bị timeout ở APN; lần sau đạt);
  - SN là `SB95B8E9A79F`, khớp với MAC `02:95:B8:E9:A7:9F`.
- **Chưa kiểm:** PPP với APN mặc định `internet`, vì SIM và mạng đang mất dịch vụ.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi). Người dùng muốn giữ đơn giản.
