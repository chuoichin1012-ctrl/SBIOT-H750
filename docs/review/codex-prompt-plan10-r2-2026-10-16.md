Plan 10, lượt 2, sau `codex-gpt-6-astra-plan10-2026-10-16.md`.

**Thông tin mới từ người dùng:**
- Module trên board là **EC600G-EU**, không phải EC600U.
- Người dùng quyết định dùng luôn timing của EC600U (PWRKEY 2,5 s, `RESET_N` 200 ms, các deadline AT), và chấp nhận nếu module chạy tốt với các timing đó trên bench. Đây là ruling 13: bench kiểm từng thông số, kể cả xung 2,5 s trên module đang chạy. Có giá trị nào sai thì sửa ở `lte_power.h` và ghi vào spec §7.2.
- **Hiện chưa có SIM.**
  - Thêm trạng thái `NO SIM`: khi `AT+CPIN?` không trả `READY` thì không quay số, và không `RESET_N`, CFUN hay RECOVER khi module vẫn trả lời. Cứ 60 s hỏi lại `AT+CPIN?`.
  - Các ca bench cần SIM (PPP, load, priority qua LTE, CFUN, fallback) để sau, xem ruling 12.

**Ba điểm của lượt 1 đã sửa trong plan:**
1. **DNS theo từng link:**
   - hook trong patch `dhcp.c` đổi thành `LWIP_HOOK_DHCP_DNS`: DHCP không còn ghi DNS toàn cục, mà đưa từng server của lease cho `netmgr` giữ;
   - DNS của PPP được chép lại trong status callback;
   - `dns_apply()` đặt lại DNS của link đang chọn sau mỗi lần đổi route, khi PPP lên, và sau mỗi DHCP ACK (kể cả renew giữ nguyên địa chỉ);
   - `netcfg_dns` là hàm thuần;
   - test cả hai thứ tự và trường hợp renew (T2, `test_dhcp_dns`).
2. **MSC sau khi mở DLC:** nhận UA của DLC1/DLC2 thì đưa một MSC command vào hàng DLC0. Lệnh này đi trước dữ liệu của DLC đó, và RUNNING chỉ đến sau MSC thứ hai. Có test thứ tự (T1).
3. **Test command lớn:** hàng DLC0 tăng lên 2112 B. Có test một Test command 2000 byte trong lúc FCoff (T1).

**Phạm vi:** `git diff 94b2735 HEAD -- stm32/docs/plans/2026-10-16-10-lte.md`.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa.
