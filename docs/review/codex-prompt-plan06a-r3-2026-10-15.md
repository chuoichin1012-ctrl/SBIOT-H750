Vòng 3 cho kế hoạch 06a. Đã sửa điểm của vòng 2.

Thứ tự trong callback của `tcpip_init`:
1. `netif_add`;
2. địa chỉ tĩnh và DNS thủ công;
3. `netif_set_up`;
4. `dhcp_start`, nếu dùng DHCP.

Kết quả của từng bước đều được kiểm. Xem plan dòng 36 và mục "Codex round 2".

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách việc phải sửa.
