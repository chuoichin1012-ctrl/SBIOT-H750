Đã sửa 4 lỗi Important của review code 08a vòng 1 (mục "Code review round 1" cuối plan `stm32/docs/plans/2026-10-16-08a-bridge.md`). Xin **review lại**: `git diff 95c946d..HEAD -- stm32/firmware`.

1. **`idleRestart`:** đọc mốc hoạt động trước, rồi mới lấy `now`; `idle_due` có host test, kể cả trường hợp mốc mới hơn `now`.
2. **Datagram UDP rỗng** giờ được tính là có việc, nên lượt đó nhường CPU. Ca bench mới `emptyudp`: dưới flood khoảng 2 triệu datagram, ADC giữ 10,00 cửa sổ/s, 0 late, đo theo đồng hồ của board. Lần RED trước đo theo đồng hồ PC (sai số ±15 %), nên trạng thái trước khi sửa chỉ được chứng minh qua đọc code.
3. **Đọc client theo vòng tròn,** tiếp tục từ chỗ lượt trước dừng; client cần loại thì loại sau khi xong lượt. Ca bench `fair` RED (một client nhận 0), giờ GREEN.
   - Ca này còn lộ thêm một lỗi: `TCP_LISTEN_BACKLOG` = 0 khiến kết nối chờ thứ 9 tràn hộp thư accept và bị reset.
   - Giờ bật backlog (listen 8), và mỗi lượt accept hết các kết nối đang chờ (tối đa 8).
4. **Mọi mốc chờ dùng 64 bit µs** (`wait_over`); host test quanh mốc 24,85 và 49,71 ngày.

Thêm:
- client bị coi là mất được đóng bằng RST (`SO_LINGER` 0, `LWIP_SO_LINGER` 1);
- ca bench client kẹt chạy cả hai client phía Windows (NAT của WSL gây dừng giả);
- ca `load` đo ADC theo đồng hồ board.

**Kết quả:** `bridge_check.py` 72/72; host test GREEN.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
