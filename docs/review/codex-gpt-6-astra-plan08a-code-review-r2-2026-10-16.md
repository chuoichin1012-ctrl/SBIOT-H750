**NOT APPROVED**

Bốn lỗi firmware đã được xử lý. Còn một lỗi **Important trong kiểm thử**:

- [bridge_check.py:212](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/bench/bridge_check.py:212): ca `stuckpair` đã bỏ kiểm tra tốc độ nhận sau phục hồi. Chỉ cần nhận 100 byte, nghỉ 10 s, nhận thêm **1 byte rồi im hẳn**, kèm log drop, assertion hiện tại vẫn PASS và báo “then got everything”. Tôi đã kiểm chứng trường hợp này.

**Cần sửa:** giữ peer Windows, nhưng khôi phục kiểm tra client khỏe tiếp tục nhận đến gần cuối thời gian phát và đạt tốc độ dự kiến sau phục hồi, chẳng hạn ngưỡng ≥90% như trước.

`test_bridge_core` hiện tại đạt; chưa chạy lại board.