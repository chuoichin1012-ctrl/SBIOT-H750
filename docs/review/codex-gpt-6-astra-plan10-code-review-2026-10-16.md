NOT APPROVED

Còn 5 lỗi **Important**:

1. **[lte.c:184](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte.c:184) — TX làm tràn RX khi truyền hai chiều.** Task đọc RX chờ gửi hết hàng TX. Riêng 4.096 byte DLC1 cộng header đã lâu hơn thời gian ring RX 4.096 byte quay một vòng ở cùng baud. Đường PPP thẳng còn có thể vét hàng liên tục. Cần xử lý RX xen giữa các frame TX; thêm test tải hai chiều làm đầy hàng gửi.

2. **[lte_power.c:31](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte_power.c:31) — NO SIM vô hiệu hóa phục hồi modem treo.** Sau khi ghi nhận NO SIM, dù modem im lặng quá 60 s, hàm vẫn trả `NONE`. Kiểm mất liên lạc trước nhánh NO SIM; chỉ miễn CFUN/reset vì thiếu SIM khi modem còn trả lời.

3. **[lte.c:386](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte.c:386) — shutdown không hoạt động trong PPP thẳng.** `ppp_stop()` không đổi `M_PLAIN_PPP`; `QPOWD` nằm trong `s_at_out`, nhưng task TX chỉ đọc `s_ppp_out`. Cần thoát data mode bằng `+++`/guard time/`ATH` theo spec trước khi gửi AT, và kiểm bằng ca ép fallback.

4. **[lte.c:231](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte.c:231) — bỏ mất phản hồi còn lại trong buffer.** Nếu một lần đọc chứa `OK\r\nPOWERED DOWN\r\n`, `at_cmd()` trả ngay tại `OK`, làm mất URC; shutdown chờ hết hạn dù modem đã tắt. Giữ phần chưa xử lý; thêm test phản hồi gộp và chia mảnh.

5. **[netmgr.c:119](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/netmgr.c:119) — ping không xác minh đúng link/nguồn.** Chỉ ID và sequence quyết định link được đánh dấu có Internet. Gói giả từ LAN có thể làm sai lựa chọn route. Cần kiểm netif nhận, địa chỉ nguồn tương ứng target, checksum và vòng probe còn hiệu lực; test gói sai link/nguồn.

Đã chạy lại sáu binary host hiện có: đều đạt. Chưa kiểm lại trên board.