NOT APPROVED

Còn một lỗi **Important** tại [lte.c:748](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte.c:748): `took_addr()` reset bộ đếm nhưng giá trị trả về bị bỏ qua, nên `last_addr` không được cập nhật khi PPP đã lên rồi xuống giữa hai lượt kiểm.

Ví dụ: đang chờ mức 10 phút, đã qua 8 phút, PPP có địa chỉ rồi rớt. Giới hạn trở về 2 phút nhưng thời gian đã chờ vẫn là 8 phút → CFUN ngay, thay vì bắt đầu khoảng chờ 2 phút mới.

**Sửa:** cập nhật `last_addr` khi `took_addr()` trả true; thêm test đúng chuỗi này. Phần DOWN sớm trên PPP thẳng đã được xử lý. Binary `test_lte_power` hiện có chạy đạt.