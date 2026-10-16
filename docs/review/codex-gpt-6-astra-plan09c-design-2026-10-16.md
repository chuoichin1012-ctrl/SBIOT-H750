**D1: đồng ý.** Giữ allocator đếm lỗi và cài trước `store_init`; bỏ arena/TLSF khỏi firmware. Khi bỏ scope, rà các đường thành công và lỗi để bảo đảm mọi cây cJSON và chuỗi in đều được giải phóng — trước đây kết thúc arena có thể che một lần quên `free`. Bỏ `arenaPeak` đồng thời khỏi UI và bench.

**D2: đồng ý với streaming**, vì đáp ứng mục tiêu buffer nhỏ và tái sử dụng được `s_chunk` 8 KB hiện có. Cần bổ sung vài quy tắc cụ thể:

- Thêm `store_edge_abort()`, gọi khi ngắt kết nối, timeout hoặc lỗi. Chỉ một phiên ghi Edge hoạt động; không giữ mutex DB giữa các lần nhận mạng.
- Generation mới phải cao hơn **mọi record đang có**, như code hiện tại; không chỉ lấy generation đã commit cộng một.
- Chỉ đổi manifest sau khi đủ body và scanner hoàn tất thành công. GET tiếp tục đọc generation đã ghim.
- Khi FAL đã khóa hoặc chưa xác định được manifest nào đã commit, giữ nguyên quy tắc hiện tại: không cố xóa thêm.
- Ngoài cắt thành hai phần tại mọi vị trí, test thêm nhận từng byte, nhiều chunk liên tiếp và kết thúc giữa UTF-8/escape/số. Scanner phải có bước kết thúc để phân biệt “chưa đủ dữ liệu” với JSON hợp lệ.

Buffer heap 48 KB đơn giản hơn về code, nhưng không đạt mục tiêu giảm đỉnh bộ nhớ của đường upload.

**D3: đồng ý.** Cấp tối đa **49.152 + 1 byte** nếu API cần NUL; giải phóng sau lần cuối bộ nạp dùng văn bản, trước khi khởi chạy task Edge. Giải phóng cả các đường thoát sớm. ASan tự đánh dấu vùng đã `free`; test cần tiếp tục dùng mô hình runtime sau đó để phát hiện con trỏ còn trỏ vào buffer.

**D4: đồng ý.** Cấp buffer sau khi kiểm auth, loại upload và giới hạn độ dài; giữ giới hạn số upload đồng thời hiện có. Với khóa riêng, zeroize buffer trước `free` cả khi thành công, lỗi, timeout và client ngắt.

**D5: đồng ý.** Giữ `s_record` 8 KB và cơ chế pin/unpin. Không cần can thiệp nội bộ FlashDB để tiết kiệm thêm phần này. Giữ điều kiện buffer được dùng tuần tự trong task web, không bị giữ qua callback.

**D6: đồng ý**, nhưng phân biệt số liệu: bỏ khoảng 112 KB, tăng heap từ 256 lên 352 KB chỉ sử dụng lại **96 KB**; phần còn lại là khoảng trống AXI, trừ thay đổi khác. Chốt kích thước bằng linker map, cập nhật spec theo số thực tế, giữ một allocator `heap_5`.

**D7: đồng ý**, bổ sung ba phép đo nhỏ:

- **Block trống lớn nhất**, không chỉ tổng heap free/minimum: còn nhiều RAM tổng cộng vẫn có thể không cấp được buffer liên tục. `vPortGetHeapStats()` cung cấp số này. [FreeRTOS heap_5](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/portable/MemMang/heap_5.c)
- Lặp một số vòng upload/GET/kết nối TLS, kiểm heap trở về mức ổn định và nhịp watchdog/IO không suy giảm.
- Ghi riêng chi phí theo **điểm, thiết bị/task và buffer report**; không suy ra giới hạn node chỉ bằng một hệ số byte/điểm vì các phần này tăng khác nhau.

Giữ giới hạn 100 điểm trong 09c. Rủi ro chính cần kiểm là rò bộ nhớ sau khi bỏ arena, phân mảnh heap và phiên upload chưa kết thúc; không cần thêm cơ chế cứu hộ mới.