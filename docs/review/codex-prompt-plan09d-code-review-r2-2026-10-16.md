Review code Plan 09d, lượt 2: sửa điểm Important của lượt 1 (`codex-gpt-6-astra-plan09d-code-review-2026-10-16.md`), commit 5326532.

**Đã xác nhận lỗi trên board:** với firmware cũ, một thiết bị RTU có 300 điểm làm board reset liên tục, khoảng 3 giây một lần.

**Sửa:**
- `edge_core`: `edge_mem_t` có thêm `order` và `batches`, mỗi mảng `npts` phần tử, được cấp phát và giải phóng cùng model trong `edge_reserve`/`edge_unreserve`. Mỗi điểm tốn thêm 24 B trên board.
- `edge_plan_batches(m, ndev)`: sau khi model đã được điền, mỗi thiết bị nhận một lát `order`/`batches` ứng với vị trí `pts` của nó, cùng `nbatch = edge_batches(...)`. `edge_start` gọi hàm này một lần, trước `resolve_formulas`.
- `poll_modbus` dùng `d->order`, `d->batches` và `d->nbatch`, không còn VLA.
- Một bản build toàn app với `-Wvla` không còn cảnh báo VLA nào.

**Test:**
- `test_edge_core` `test_plan_batches`: 2 thiết bị, tổng 300 điểm (290 có merge, 10 không merge). Kế hoạch đọc trùng kết quả của `edge_batches`, và được giải phóng cùng model. Test đỏ trước khi sửa.
- Bench `edge_check onedevice`: 300 điểm trên một thiết bị RTU, rồi trên một thiết bị Modbus TCP, đều bật merge.
  - cả hai đọc đủ RUNNING 300/300;
  - stack còn dư: `edge_bus1` 5 020 B, `edge_tcp0` 2 856 B.
- `edge_check basic remote memory` đạt. Các task bus giờ còn dư 5 020 B stack, trước đó 2 108 B.
- `mem_check` đạt:
  - (e): heap free 127 912, thấp nhất 64 296, khối lớn nhất 107 120;
  - 4/4 lần MQTT kết nối lại, 37 report.

**Bench:** `mem_check` có thêm hai thay đổi.
- Mức heap lấy giá trị free lớn nhất trong 3 lần đọc cách nhau 1 s. Lý do: có lần một mẫu rơi đúng lúc phiên TLS của UART2 (khoảng 27 KB) đang mở.
- Một lần kết nối lại được tính khi xuất hiện dòng "connected" mới hơn dòng trước đó. Cách đếm số dòng cũ không dùng được, vì ring log RAM 32 KB bị các dòng HTTPS của UART2 đẩy trôi trong khoảng 3 phút.

**Phạm vi:** `git diff 74c2d8e HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi). Nhớ là người dùng muốn giữ đơn giản.
