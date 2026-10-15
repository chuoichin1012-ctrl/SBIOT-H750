NOT APPROVED

Cần sửa bốn điểm trong kế hoạch:

1. **Thứ tự khởi tạo PHY — dòng 19–22:** đổi thành **reset/release PHY → `HAL_ETH_Init` → quét PHY**. Hiện quét trước khi HAL bật clock/GPIO và đặt bộ chia MDIO, nên có thể báo không tìm thấy PHY. Nguồn: [HAL_ETH_Init](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/stm32cube_h7/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_eth.c:314).

2. **Ngữ cảnh lwIP — dòng 33–41:** không gọi trực tiếp `netif_add`, `dhcp_start` từ task `init`. Ghi rõ thực hiện trong callback của `tcpip` sau khi khởi tạo xong; đọc status cũng phải đồng bộ. Hai API này yêu cầu giữ core lock; `tcpip_init` khởi tạo thread bất đồng bộ. Nguồn: [netif.c:297](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/lwip/src/core/netif.c:297), [tcpip.c:659](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/lwip/src/api/tcpip.c:659).

3. **Hook DNS — dòng 39:** không cập nhật DNS chỉ vì hook vừa đọc được option 6. Hook chạy trước khi toàn bộ DHCP message được kiểm và chấp nhận; phần phía sau vẫn có thể lỗi. Cần quy định chỉ áp dụng từ ACK hợp lệ, thêm test DNS tự động, ACK gia hạn, gói lỗi sau option 6 và DNS thủ công không đổi. Nguồn: [dhcp.c:1647](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/lwip/src/core/ipv4/dhcp.c:1647). Có thể kiểm trên host khi bench chưa có DHCP.

4. **RNG — dòng 42:** giới hạn fallback xorshift cho `LWIP_RAND`; không dùng nó làm entropy cho mbedTLS. Đường entropy TLS phải báo lỗi khi RNG phần cứng thất bại, phù hợp §7.3. UID và thời gian không thay thế nguồn entropy mạnh. Nguồn: [tài liệu Mbed TLS](https://mbed-tls.readthedocs.io/en/latest/kb/how-to/add-entropy-sources-to-entropy-pool/).

Các lựa chọn còn lại — chuyển pool sang AXI, RX free list riêng, checksum phần mềm, MAC từ UID, stack tĩnh và giữ 4 descriptor để đo — chấp nhận được ở mức kế hoạch. Chưa cần tăng độ phức tạp hay mở rộng chạy test ngoài phần bị ảnh hưởng.