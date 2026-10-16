NOT APPROVED

Phần DNS còn ba trường hợp cần sửa trong ruling 5/T2:

1. **PPP không cấp DNS:** `ipcp_up()` chỉ gọi `sdns()` khi có ít nhất một DNS. Sao chép DNS toàn cục trong callback có thể lấy nhầm DNS Ethernet làm DNS cell. Cần lấy cặp DNS từ kết quả thương lượng IPCP, kể cả cặp rỗng.

2. **PPP xuống nhưng route vẫn là Ethernet:** `cdns()` có thể xóa DNS toàn cục nếu trùng DNS của PPP. Cần áp lại DNS sau khi PPP hoàn tất dọn DNS, dù route không đổi. Thêm test Ethernet và PPP dùng cùng DNS. Nguồn cho hai trường hợp: lwIP 2.2.1 [`ipcp.c`](https://github.com/lwip-tcpip/lwip/blob/STABLE-2_2_1_RELEASE/src/netif/ppp/ipcp.c), [`ppp.c`](https://github.com/lwip-tcpip/lwip/blob/STABLE-2_2_1_RELEASE/src/netif/ppp/ppp.c).

3. **DHCP ACK có ít DNS hơn lần trước:** hook gọi riêng từng server chưa bảo đảm xóa slot cũ, và không được gọi nếu ACK không có DNS. Cần chuyển giao một cặp hoàn chỉnh sau mỗi ACK được chấp nhận; test renew từ hai DNS xuống một và xuống không có DNS.

MSC trước dữ liệu và buffer Test command đã xử lý đúng. Chấp nhận timing theo ruling 13 và việc ghi **NOT RUN** cho các ca cần SIM theo phạm vi mới.