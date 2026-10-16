Plan 09e đã merge (PR #20, 200 điểm Edge). Theo roadmap, tiếp theo là **Plan 10: LTE**.

Plan nháp: `stm32/docs/plans/2026-10-16-10-lte.md`, trên branch `plan10-lte`. Plan dựa trên spec §7.2, §5.3, §6.1 và §12.1–12.2 đã duyệt.

**Tóm tắt:**
- **Phần C thuần, có host test:**
  - `proto/cmux.c`: codec và state machine 27.010, TX theo kiểu pull (`cmux_tx_next`);
  - `proto/at_parse.c`: phân tích phản hồi AT;
  - `netmgr/lte_power.c`: trình tự bật nguồn ON/OFF/UNKNOWN và quyết định phục hồi;
  - `netcfg_pick`: chọn route.
- **Trên board:** `lte_uart` (UART3, DMA vòng 4 KB), task `cmux_rx` (prio 25), task `lte` (prio 5), PPPoS của lwIP vendored từ đúng tarball 2.2.1, và `netmgr` chọn default route và DNS.
- **Đổi route:** abort mọi PCB TCP đang hoạt động có địa chỉ local không thuộc netif mặc định mới. Các dịch vụ tự kết nối lại theo vòng lặp sẵn có.

**Cần duyệt:**
- 12 ruling, đặc biệt:
  - (1) `cmux_rx` là chủ duy nhất của UART3 TX;
  - (5) DNS của cell;
  - (6) cách đóng kết nối khi đổi route;
  - (10) gọi `AT+QPOWD=1` trước mỗi lần restart;
  - (12) điều kiện bench: cần EC600U, SIM có data, và broker công khai qua LTE.
- Mục Review Focus.
- Các task T1–T6.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa. Nhớ là người dùng muốn giữ đơn giản.
