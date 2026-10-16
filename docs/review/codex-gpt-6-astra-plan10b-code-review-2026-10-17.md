NOT APPROVED

Còn 2 lỗi **Important**:

1. **[lte.c:629](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte.c:629) — PPP thẳng bỏ qua thời gian backoff.** `EV_PPP_DOWN` trả ngay `PLAIN_NO_ADDR`, khiến caller tăng lượt rồi RESET/power-cycle dù chưa hết hạn. PPP bị từ chối xác thực sớm sẽ tiếp tục phục hồi liên tục. Event DOWN còn sót từ `ppp_stop()` của phiên trước cũng có thể kích hoạt đường này. Cần loại event cũ và chỉ tăng lượt khi deadline hết; thêm test DOWN sớm và DOWN tồn từ phiên trước.

2. **[lte.c:624](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte.c:624), [lte.c:730](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte.c:730) — có thể bỏ mất lần PPP đã có địa chỉ.** Bộ đếm chỉ reset khi task LTE thấy `s_ppp_up`. Nếu PPP lên rồi xuống giữa hai lượt kiểm, backoff vẫn giữ mức cũ, tiếp tục power-cycle thay vì trở về 2 phút/CFUN. Cần ghi nhận sự kiện có địa chỉ để task LTE xử lý, kể cả sau DOWN; test UP→DOWN trước lượt kiểm tiếp theo.

Binary `test_lte_power` hiện có chạy đạt. Việc chưa kiểm lại PPP trên board do SIM/mạng không phải lỗi chặn riêng.