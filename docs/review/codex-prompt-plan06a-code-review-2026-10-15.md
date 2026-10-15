Plan 06a đã code xong và chạy trên board. Branch `plan06-network-web`. Trước khi tạo PR, xin **review code**:
- `git diff main..HEAD -- stm32/`;
- plan `stm32/docs/plans/2026-10-15-06a-ethernet-lwip.md`: phát hiện B1–B5, đo đạc, ruling 1–5;
- spec §5.3, §5.6 (bảng pool), §7.1.

**Trọng tâm:**
1. `components/netmgr/eth_drv.c`:
   - pool RX free list và `rx_free` (đánh thức khi hết buffer);
   - TX chép vào buffer riêng, `HAL_ETH_ReleaseTxPacket`/`TxFreeCallback`, chờ 20 ms rồi `ERR_MEM`;
   - `eth_rx`: tối đa 8 frame, `vTaskDelay(1)`, link poll 1 s dưới core lock, yêu cầu power từ console chạy trong `eth_rx`;
   - thứ tự init: reset PHY, `HAL_ETH_Init`, quét MDIO.
2. `components/netmgr/netmgr.c`:
   - callback `tcpip_init` theo thứ tự đã duyệt;
   - stack tĩnh của `tcpip`;
   - `netmgr_dns_from_lease`;
   - status dưới core lock;
   - `lwip_ram_heap` dư 64 B so với `MEM_SIZE`: kiểm giúp xem có đủ cho `mem.c` không.
3. `app/lwipopts.h` và `app/arch/cc.h`: bảng pool, `LWIP_DECLARE_MEMORY_ALIGNED` đặt ở AXI, `fd_set`/`timeval` của newlib.
4. Patch lwIP `third_party/patches/lwip-2.2.1-sbiot.patch`, và `test_dhcp_dns`.
5. `components/board/board_rng.c`:
   - mutex;
   - khởi động lại RNG một lần khi lỗi seed hay clock;
   - fallback xorshift chỉ cho `LWIP_RAND`.
6. `ip101.c`: quét 1–31 rồi 0 (B3), `test_ip101`, `test_netcfg`.
7. CubeMX (`tools/mkioc.py`): ETH NVIC 6, RNG HSI48 (`MX_RNG_Init` không gọi), FIFO UART7 chỉ ở app (B1); `check_pins` chỉ đóng băng định dạng đường truyền của UART7.

**Đo trên board:**
- `net_check.py` đạt: ping, ping phân mảnh, IP tĩnh từ cấu hình sau khi khởi động lại, 5 lần mất link, flood UDP 20 s khoảng 11 400 gói/s; wdog vẫn 200 ms; RX về lại 4 buffer;
- `store_check.py` đạt.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa (lỗi thật, theo mức độ). Nhớ người dùng muốn giữ đơn giản.
