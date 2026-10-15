NOT APPROVED

Cần bổ sung hai điểm để triển khai an toàn:

1. **Arena — plan dòng 46, ruling 5:** [arena.c:19](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/arena/arena.c:19) hiện dùng `s_current` toàn cục. Khi web mở arena, UART7 hoặc console gọi cJSON cũng sẽ cấp phát vào đó, trái với giả định “vẫn dùng heap”.  
   **Sửa plan:** thêm bước nhận diện task sở hữu arena; scope chỉ nằm trong **một callback**, không kéo dài suốt request streaming. Test xen kẽ web/UART phải chứng minh cấp phát của task khác vẫn thuộc heap.

2. **Client đọc chậm — plan dòng 53, 65:** gửi chunk và MessageBuffer 2 KB không giới hạn `c->send`. Mongoose nối dữ liệu gửi vào buffer heap; client WebSocket ngừng đọc có thể khiến heap tăng liên tục.  
   **Sửa plan:** GET Edge chỉ gửi tiếp khi `c->send.len < 4 KB` như §9.4; UI/log cũng phải gửi có giới hạn. WS vượt ngưỡng thì bỏ frame hoặc đóng client. Mọi đường đóng/lỗi phải bỏ pin. Thêm bench client không đọc, kiểm heap ổn định và client khác vẫn hoạt động. Nguồn: [Mongoose](https://mongoose.ws/documentation/).

**Các lựa chọn còn lại chấp nhận được:** ruling 1–4, idiom `c->pfn=NULL`, đồng hồ không dùng TZ và `.incbin`.

Khi viết các bước thực thi, ghi thêm hai ràng buộc ngắn:

- Kiểm giới hạn body trước khi Mongoose tự đóng vì `MG_MAX_RECV_SIZE`; thêm test `Content-Length` và chunked vượt 12 KB, vẫn nhận đúng 413.
- SNTP chạy trong ngữ cảnh lwIP core; tên server phải sống suốt runtime, offset giờ 64-bit phải được đọc/ghi đồng bộ.