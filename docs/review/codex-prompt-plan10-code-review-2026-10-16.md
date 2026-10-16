Plan 10 (LTE) đã làm xong trên branch `plan10-lte`, từ 7a6d557 (plan được duyệt) tới HEAD. Đây là bước review code bắt buộc trước PR.

Plan: `stm32/docs/plans/2026-10-16-10-lte.md`, gồm ruling 13 và 14 và các mục "Measurements" và "Bench findings".

**Các phần chính:**
- **T1** `proto/cmux.c`: codec và state machine 27.010. Có một sửa từ bench: trường độ dài 2 byte, byte thứ hai là L8..L15, không có bit EA (§5.2.1.5). Test lấy từ một frame thật mà modem gửi.
- **T2:**
  - `at_parse.c`;
  - `lte_power.c`: trình tự ON/OFF/UNKNOWN, không xung nào từ 3 s trở lên;
  - `netcfg_pick` và `netcfg_dns`;
  - patch `dhcp.c`: hook `LWIP_HOOK_DHCP_DNS` nhận nguyên danh sách DNS, mỗi ACK một lần.
- **T3:**
  - lwIP PPP vendor từ đúng tarball;
  - `lte_uart` (DMA RX vòng 4 KB, DMA TX);
  - `cmux_rx` là chủ duy nhất của UART3 và của `cmux_t`, các task khác chỉ trao đổi qua stream buffer;
  - task `lte`;
  - sửa chân theo sơ đồ: PWRKEY là PA6, RESET_N là PA5 (trước ghi sai PA5/PA4).
- **T4:**
  - PPP trên DLC1 (`ATD*99#`), PPP thẳng dự phòng;
  - `netmgr` chọn route theo priority sau mỗi sự kiện link, ghi cặp DNS của link được chọn, abort các PCB TCP ở link cũ;
  - chỉ quay số khi module đã đăng ký mạng.
- **T4b:** kiểm Internet bằng ICMP qua từng link (ruling 14).
- **T5:**
  - trước khi restart: CLD, `AT+QPOWD=1` trên UART thường, chờ `POWERED DOWN`;
  - priority 0 tắt module nếu nó trả lời;
  - sau CFUN, AT trên DLC2 không trả lời thì RECOVER ngay;
  - lệnh bench `K`, `V`, `M`, `Q`, `q`, `H`.
- **T6:** `lte_check.py`, deadline 10 s, ghi số đo.

**Kết quả:**
- **Host:** `test_cmux`, `test_at_parse`, `test_lte_power`, `test_netcfg`, `test_dhcp_dns`, `test_proto` đều xanh. App build được, `check_elf` và `check_pins` đạt.
- **Board** (board thứ hai, EC600G-EU, SIM VinaPhone):
  - `lte_check` đạt tất cả các ca;
  - `load`: 1 090 000 byte đi xuống qua LTE trong 5 phút, overrun 0, bad FCS 0;
  - `web_check plain configs` và `net_check` đạt.
- **Chưa chạy:**
  - chiều lên của `load` và các bài hồi quy cần vòng nối UART1↔UART2, vì board này không có dây nối RS485;
  - PPP thẳng dự phòng, vì module chấp nhận `AT+CMUX`.

**Phạm vi:** `git diff 7a6d557 HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi). Nhớ là người dùng muốn giữ đơn giản.
