Tiếp tục trong cùng phiên. Plan 05b đã merge (PR #8). Bước tiếp theo là **Plan 06: mạng và web**. Tôi tách thành:
- **06a:** Ethernet IP101 + lwIP;
- **06b:** Mongoose, `/api/*`, UI và đồng hồ.

Xin duyệt **kế hoạch 06a trước khi code**: `stm32/docs/plans/2026-10-15-06a-ethernet-lwip.md`, trên branch `plan06-network-web`. Spec liên quan: §5.3, §5.4, §5.6, §6.1, §7.1, §9.1.

**Người dùng đã quyết:**
- mọi cấu hình, kể cả login, chỉ áp dụng sau khi gateway khởi động lại. Spec §10.1 đã sửa trên branch này, chưa commit;
- giữ dự án đơn giản;
- chỉ chạy các test bị ảnh hưởng;
- driver PHY tham khảo template trong bộ HAL H7 (`Drivers/BSP/Components/lan8742`).

**Các điểm tôi muốn được kiểm, và điểm tôi đề xuất đổi spec:**
1. **Bảng pool.** Các pool memp và `PBUF_POOL` là dữ liệu chỉ CPU dùng, nên tôi chuyển chúng sang AXI. SRAM1 chỉ giữ những gì DMA chạm tới, cộng heap `MEM` 64 KB, để còn chỗ cho USART ở Plan 07. Việc này đổi spec §5.3.
2. **RX zero-copy dùng free list riêng ở SRAM1, không dùng `LWIP_MEMPOOL`.** Pbuf free đánh thức `eth_rx` khi HAL hết buffer, nếu không RX chết sau một trận flood.
3. **Checksum tính bằng phần mềm,** TX không chèn checksum phần cứng.
4. **MAC lấy từ UID:** `02` cộng 5 byte đầu của SHA-256(UID). Spec chưa nói gì về MAC.
5. **DNS thủ công khi dùng DHCP:** đặt `LWIP_DHCP_MAX_DNS_SERVERS` 0, xin option 6 qua `DHCP_ADD_EXTRA_REQUEST_OPTIONS`, và dùng hook `LWIP_HOOK_DHCP_PARSE_OPTION`. Không vá lwIP.
6. **Patch lwIP chỉ một hàm** (`sys_thread_new`), để stack của `tcpip` tĩnh ở DTCM.
7. **RNG phần cứng:** bật HSI48 bằng macro trong MspInit, vì `HAL_RCC_OscConfig` bị cấm trong app.
8. **Bench:**
   - PC nối thẳng với board, IP tĩnh 192.168.0.10, không có DHCP; WSL ở chế độ NAT nên ping qua `ping.exe` của Windows;
   - DHCP chỉ kiểm khi có LAN có DHCP (`--dhcp`);
   - link được ngắt bằng `BMCR.PD` qua console, không cần rút cáp.
9. Giữ 4 descriptor RX theo mặc định CubeMX, và đo ở bài flood.

Trả lời: **APPROVED**, hoặc **NOT APPROVED** kèm danh sách việc phải sửa. Ưu tiên lỗi thật hơn chi tiết, và nhớ người dùng muốn đơn giản.
