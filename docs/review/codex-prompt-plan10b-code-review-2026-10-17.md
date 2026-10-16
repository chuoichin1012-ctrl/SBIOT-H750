Plan 10b (LTE backoff và soft power cycle) đã làm xong trên branch `plan10b-lte-backoff`, từ 4413617 (plan được duyệt ở r2) tới HEAD. Đây là bước review code bắt buộc trước PR.

Plan: `stm32/docs/plans/2026-10-17-10b-lte-backoff.md`. Spec §7.2, mục "Những gì Plan 10b thêm".

**Các phần chính:**
- **T1** `lte_power.c`:
  - `lte_backoff_limit`: chờ 2, 5, 10, rồi 30 phút;
  - `lte_backoff_step`: CFUN cho hai lần đầu, POWER_CYCLE từ lần thứ ba;
  - `lte_backoff_ok`;
  - `lte_supervise` nhận giới hạn làm tham số;
  - test trong `test_lte_power`.
- **T2** `lte.c`:
  - qua CMUX, khi tới giới hạn thì gọi `lte_backoff_step`. Trả POWER_CYCLE thì gọi `power_down()` (CLD, QPOWD, chờ `POWERED DOWN`). Thành công thì trình tự khởi động đi từ OFF, tức PWRKEY. Thất bại thì gọi `recover()`, trạng thái thành UNKNOWN; không nhấn PWRKEY mù;
  - đường UART thường (`plain_ppp`) dùng cùng `s_backoff`: thời gian chờ đăng ký và chờ địa chỉ đều lấy giới hạn của backoff. Hai lần đầu là RESET_N, từ lần thứ ba là soft power cycle (`power_down` thoát data mode bằng `+++`/ATH trước);
  - PPP có địa chỉ thì `lte_backoff_ok` và `recoveries = 0`;
  - `cell.recoveries` trong `/api/status`;
  - lệnh console `b` (chia thời gian chờ cho 10) và `d` (`power_down` bỏ qua `POWERED DOWN`).
- **Ruling:**
  - trên UART thường, chờ hỏi mỗi 5 s (POLL_MS là 20 s, quá thô so với 12 s ở chế độ bench);
  - PPP rớt sau khi đã có địa chỉ trên UART thường thì RESET_N, không tính là một bước backoff.

**Kết quả:**
- **Host:** `test_lte_power` xanh. App build được.
- **Board:**
  - `lte_check backoff`: CFUN sau 12 s, sau 30 s, rồi soft power cycle sau 60 s. `POWERED DOWN` đến, rồi PWRKEY, không có RESET_N chen giữa. `recoveries` là 3;
  - `backoff_nopd`: RECOVER, PWRKEY chỉ sau khi RESET_N và AT đều không được trả lời;
  - restart trong data mode: `+++`, ATH, rồi `POWERED DOWN`.
- **Chưa kiểm:** PPP lên lại sau khi phục hồi, và các ca cần PPP. Trong lúc chạy bench, SIM và mạng mất dịch vụ (CSQ 19–23 nhưng `+CEREG: 0,2`). Người dùng xác nhận đây là vấn đề của SIM và mạng, và bảo tiếp tục.

**Phạm vi:** `git diff 4413617 HEAD -- stm32/firmware`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi). Nhớ là người dùng muốn giữ đơn giản.
