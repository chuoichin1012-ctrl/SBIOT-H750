NOT APPROVED

Còn 3 điểm **Important**:

1. **Rò heap khi dựng JSON gặp OOM.** [api.c:109](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/api.c:109), tương tự dòng 170, 230 và [iomgr.c:244](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/iomgr/iomgr.c:244). Nếu tạo mảng cha thất bại, các object con vẫn được tạo nhưng `cJSON_AddItemToArray` trả false; chúng không được giải phóng. Trước đây arena thu hồi phần này, nay heap mất vĩnh viễn. **Sửa:** dừng dựng cây khi thiếu cha hoặc giải phóng child khi attach thất bại; thêm test lỗi cấp phát cho `/api/status` và `/api/io`.

2. **Mô phỏng reboot chưa xóa trạng thái phiên streaming.** [store.c:440](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:440). Cắt điện giả bằng `longjmp` trong lúc ghi body trên 8 KB để lại `s_es.open=true` qua `store_deinit/init`; lần lưu tiếp theo trả BUSY. MCU thật xóa BSS nên đây là lỗi mô phỏng, không phải kết luận firmware mất dữ liệu. Ma trận hiện dùng body dưới 8 KB nên chưa chạm đường này. **Sửa:** reset trạng thái phiên trong lifecycle khởi tạo lại, không xóa flash; thêm ca cắt giữa các record rồi reboot và lưu tiếp.

3. **Bench RAM có thể PASS khi tải thử đã thất bại.** [mem_check.py:103](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/bench/mem_check.py:103) bỏ qua kết quả POST/GET; sau khi ngắt relay cũng không xác nhận MQTT kết nối lại. `case_heap_loop` tương tự bỏ qua status HTTP. **Sửa:** kiểm status mong đợi, nội dung GET và từng lần reconnect. Đồng thời chứng minh HTTPS còn hoạt động trong tổ hợp đo upload/GET; hiện HTTPS chỉ kích một lần rồi chờ 10 giây trước phép đo đó.

Đã chạy đạt `test_jsonscan`, `test_edge_text`, `test_text_small`, `test_edge_core`. Lượt `STORE_CUTS` chưa hoàn tất và đã được dừng; không chạy lại bench board.