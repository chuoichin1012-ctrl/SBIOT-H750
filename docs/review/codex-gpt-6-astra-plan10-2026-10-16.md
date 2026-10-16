NOT APPROVED

Cần sửa ba điểm:

1. **DNS — ruling 5, `route_check()` và T4.** PPP `sdns()` và DHCP ACK đều ghi DNS toàn cục. Vì vậy PPP lên khi Ethernet vẫn được ưu tiên, hoặc DHCP renew khi LTE đang được ưu tiên, có thể đổi sai DNS **mà route không đổi**. Cần lưu DNS riêng từng link và áp lại DNS của link được chọn sau các sự kiện này; thêm test cả hai thứ tự, kể cả renew giữ nguyên IP. Nguồn: [lwIP 2.2.1 `ppp.c`](https://github.com/lwip-tcpip/lwip/blob/STABLE-2_2_1_RELEASE/src/netif/ppp/ppp.c), [DHCP hiện tại](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/lwip/src/core/ipv4/dhcp.c:741).

2. **Thiếu MSC khi mở DLC — T1.** Plan chuyển sang RUNNING sau UA rồi gửi dữ liệu, nhưng 27.010 §5.4.6.3.7 yêu cầu gửi MSC trước dữ liệu người dùng sau khi tạo DLC. Bổ sung bước này và test thứ tự MSC trước AT/PPP. Nguồn: [3GPP TS 27.010](https://www.etsi.org/deliver/etsi_ts/127000_127099/127010/18.00.00_60/ts_127010v180000p.pdf).

3. **Hàng đợi DLC0 128 byte không đáp ứng Test command lớn — ruling 3/T1.** Với N1=2048, Test hợp lệ có thể mang hơn 128 byte; response phải trả nguyên verification pattern theo §5.4.6.3.4. Cần bố trí chỗ giữ response đủ lớn, hoặc cách gửi tương đương; thêm test Test command lớn trong lúc FCoff. Nguồn: [3GPP TS 27.010](https://www.etsi.org/deliver/etsi_ts/127000_127099/127010/18.00.00_60/ts_127010v180000p.pdf).

Các lựa chọn một chủ UART3 TX, TX kiểu pull và đóng TCP trong tcpip thread phù hợp hướng giữ đơn giản.