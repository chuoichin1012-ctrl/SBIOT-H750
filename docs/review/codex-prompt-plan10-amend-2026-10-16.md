Plan 10, sửa đổi giữa chừng: ruling 14 và task T4b, theo quyết định của người dùng.

**Tiến độ:** T1, T2, T3 đã xong. T4 chạy trên board với SIM mới:
- PPP lên;
- priority 2 route qua LTE, MQTT và NTP chạy qua LTE;
- priority 1 chuyển LTE ↔ Ethernet theo link.

Ngoài ra đã sửa một lỗi CMUX phát hiện trên bench: byte độ dài thứ hai của trường length 2 byte không có bit EA, theo 27.010 §5.2.1.5. Test lấy từ một frame thật mà modem gửi.

**Vấn đề:** LAN Ethernet của bench vẫn có địa chỉ nhưng không ra Internet. Với priority 1, gateway cứ bám Ethernet. Spec §7.2 ghi đây là giới hạn giữ từ bản ESP32. Người dùng yêu cầu thêm phần kiểm Internet và đồng ý cách làm sau: ping ICMP cố định tới 8.8.8.8 và 1.1.1.1 mỗi 30 s trên từng link; không có tùy chọn trên UI.

**Đề xuất cần duyệt:** `stm32/docs/plans/2026-10-16-10-lte.md`, ruling 14 và task T4b.
- Timer lwIP trong thread tcpip, không thêm task.
- Ping đi qua đúng link bằng `raw_sendto_if_src`.
- 3 vòng liên tiếp không trả lời thì coi là mất Internet, 1 lần trả lời là có lại.
- Chỉ rời link ưu tiên khi nó mất Internet **và** link kia có cả địa chỉ lẫn Internet. Nếu cả hai đều không ping được thì chọn theo địa chỉ như cũ. Priority 0 vẫn chỉ dùng Ethernet.
- Thêm `eth.internet` và `cell.internet` vào `/api/status`.

**Phạm vi:** `git diff HEAD~1 HEAD -- stm32/docs/plans/2026-10-16-10-lte.md`.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa. Nhớ là người dùng muốn giữ đơn giản.
