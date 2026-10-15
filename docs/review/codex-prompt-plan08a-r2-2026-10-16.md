Đã sửa 3 điểm của vòng 1 trong `stm32/docs/plans/2026-10-16-08a-bridge.md` (mục "Codex round 1" cuối plan; `git diff 7bbfb09..HEAD -- stm32/docs/plans/2026-10-16-08a-bridge.md`):

1. **Gửi không chặn** (`MSG_DONTWAIT`, không `SO_SNDTIMEO`), có offset riêng cho mỗi kết nối, qua `tx_progress` (host test):
   - `EWOULDBLOCK`, `EAGAIN`, `ENOMEM`, `ENOBUFS` giữ dữ liệu và socket; gửi một phần rồi `NET_AGAIN` không mất và không lặp byte;
   - chỉ peer không nhận thêm byte nào trong 10 s mới bị coi là mất;
   - bridge chỉ lấy frame kế tiếp khi mọi kết nối đã gửi xong frame hiện tại; frame chờ trong MessageBuffer, phần tràn bị bỏ và đếm ở subscriber, không bao giờ chặn `serial_rx[n]`;
   - `select()` chờ tối đa 10 ms, kèm điều kiện ghi được cho các kết nối đang có frame dở, nên task ngủ trong lúc chờ;
   - UDP: `NET_AGAIN` giữ datagram tới vòng sau (quá 10 s thì bỏ và đếm), không bao giờ gửi một phần.
2. **Dùng tên lwIP ở mọi chỗ:** `closesocket()` / `lwip_fcntl()`. `check_elf.py` kiểm `app.elf` không link `close`/`_close`/`fcntl`/`_fcntl`.
3. **Gate nhận kết nối §5.6:** `accept_allowed(free, reserve = 4)` (host test); listener chỉ nằm trong tập `select()` khi được phép; lệnh bench `Y<n>` nâng ngưỡng để kiểm trên bench.

Stack của `bridge[n]` (khoảng 12 KB, AXI) sẽ được ghi vào dòng AXI của §5.3.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
