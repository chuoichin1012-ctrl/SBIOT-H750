**NOT APPROVED**

Còn một lỗi trong cách phân loại: **`EAGAIN/EWOULDBLOCK` không chứng minh cửa sổ peer đầy.** lwIP đang dùng chuyển `tcp_write → ERR_MEM` thành `ERR_WOULDBLOCK` khi gửi non-blocking chưa gửi được byte nào ([api_msg.c:1775](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/lwip/src/api/api_msg.c:1775)). Vì vậy thiếu bộ nhớ vẫn có thể bị phân loại `NET_FULL` rồi đóng socket sau 10 s; test fake `NET_NOMEM` không bắt được.

Cách sửa đơn giản: ghi rõ timeout 10 s là chính sách **không tiến triển**, có thể bao gồm thiếu bộ nhớ nội bộ; đồng bộ ngoại lệ trong §5.6, bỏ khẳng định “peer không đọc”, và thêm test đường ánh xạ thực của lwIP. Nếu vẫn muốn bảo đảm tuyệt đối không đóng khi thiếu bộ nhớ, cần giữ được nguyên nhân lỗi từ lwIP.

Giới hạn công việc mỗi lượt và nhường CPU đã phù hợp; tính cả lần thử gửi thất bại là công việc để vẫn thực hiện `vTaskDelay(1)`.