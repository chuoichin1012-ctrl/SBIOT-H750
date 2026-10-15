Vòng 2 cho kế hoạch 06a (`stm32/docs/plans/2026-10-15-06a-ethernet-lwip.md`). Bốn điểm của vòng 1 đã sửa, xem mục "Codex round 1" ở cuối plan:

1. **Thứ tự init:** release PHY, rồi `HAL_ETH_Init`, rồi mới quét MDIO.
2. **Ngữ cảnh lwIP:**
   - `netif_add`, `dhcp_start`, đặt DNS và `netif_set_up` chạy trong callback của `tcpip_init`, task `init` chờ semaphore;
   - thay đổi link đi qua `tcpip_callback`;
   - status đọc dưới `LOCK_TCPIP_CORE()`.
3. **DNS:** bỏ hook parse. Thay bằng patch 2 dòng trong `dhcp_handle_ack`: vòng lặp DNS chỉ chạy khi `LWIP_DHCP_DNS_FROM_LEASE(netif)` (mặc định 1, bên tôi là `!dns_manual`). Hàm này chỉ được gọi cho ACK đã parse xong và được chấp nhận.
   - Test trên host `test_dhcp_dns` chạy DHCP thật của lwIP (`NO_SYS`=1) với gói tự sinh, gồm: DNS tự động, ACK gia hạn, ACK có option hỏng sau option 6, DNS thủ công không đổi.
   - RED: stub macro về 1.
4. **RNG:**
   - `board_rng(buf, len)` báo lỗi khi RNG phần cứng lỗi, và là nguồn duy nhất cho mbedTLS;
   - xorshift chỉ dùng cho `LWIP_RAND`.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách việc phải sửa.
