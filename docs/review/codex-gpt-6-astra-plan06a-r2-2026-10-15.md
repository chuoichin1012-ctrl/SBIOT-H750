NOT APPROVED

Còn một điểm cần sửa: **đưa `netif_set_up()` trước `dhcp_start()`** trong callback khởi tạo.

[Plan dòng 36](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-06a-ethernet-lwip.md:36) đang liệt kê thứ tự ngược lại. lwIP kiểm `netif_is_up()` ngay đầu [dhcp_start()](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/lwip/src/core/ipv4/dhcp.c:821); gọi khi interface chưa up sẽ assert hoặc trả `ERR_ARG`, khiến DHCP không khởi động.

Ghi rõ thứ tự: `netif_add` → cấu hình địa chỉ/DNS → `netif_set_up` → `dhcp_start` nếu dùng DHCP; kiểm kết quả trả về.

Bốn hướng sửa vòng 1 đều chấp nhận được, gồm patch DNS và việc tách RNG cho TLS. Không thấy lỗi chặn khác trong phần kế hoạch vừa sửa.