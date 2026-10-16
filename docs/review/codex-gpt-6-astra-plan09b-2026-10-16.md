NOT APPROVED

Hướng chia task hợp lý; không cần nhúng toàn bộ code vào plan. Cần sửa rõ 5 điểm trước khi thực hiện:

1. **Tách bản lưu khỏi bản runtime — ruling 4, T2/T3.** Hiện `store_get_json()` và `store_get()` dùng chung cache. Plan cần ghi rõ đường GET đọc bản lưu nguyên byte, kể cả cấu hình `INVALID`; bộ nạp dùng bản đã kiểm và điền mặc định. Thêm test: sau reboot, GET cấu hình sai vẫn trả đúng bản đã lưu; điền key mới không làm đổi GET; lưu cấu hình mới không đổi runtime hay trạng thái kiểm của lần boot hiện tại.

2. **Thống nhất điều kiện và phạm vi safe mode — ruling 5/6.** Ruling 5 yêu cầu safe mode khi `network` sai, nhưng ruling 6 chỉ xét `io`/`system`. Bổ sung `network` và lỗi DB cfg. “DO held off” phải áp dụng cả lệnh `/api/io`, Modbus IO và linkage, không chỉ bỏ timer/`keepOutputs`. Thêm test cố bật DO trong safe mode và kiểm Reload vẫn hoạt động. Với port bị tắt, kiểm MQTT/Edge không gọi vào driver chưa khởi tạo.

3. **Ruling 10 chưa phù hợp cơ chế chịu lỗi hiện tại.** FAL đã khóa mọi ghi/xóa tới reboot sau lỗi đọc; không thể hứa Reload sẽ format DB ngay. Chốt: không bỏ khóa FAL; reset không thực hiện được thì báo `incomplete` và reboot. Cũng phải xử lý DB chưa khởi tạo thành công trước khi gọi `fdb_kv_set_default`, vì `store_init` hiện thoát sớm khi cfg mount lỗi. Thêm test mount cfg thất bại và Reload, bảo đảm không truy cập DB chưa sẵn sàng, không ghi/xóa khi FAL đã khóa.

4. **Kiểm template chưa đủ rõ — ruling 3/5/8, T4.** Số slot đơn thuần không quyết định kích thước report: còn kiểu điểm, chuỗi, escaping, placeholder và template. UI cần tính theo cùng quy tắc giới hạn **26.624 byte** của bộ biên dịch C, với fixture có mô hình đi kèm. Đồng thời chốt template lỗi/quá lớn thì **tắt toàn Edge** theo ruling 5 hay **chỉ tắt report** như spec §9.4 hiện tại; sửa spec và test theo lựa chọn đó.

5. **Bổ sung hai gate dữ liệu quan trọng.**
   - T1 phải cắt điện tại mọi thao tác **xóa lẫn ghi**, kể cả xóa dở sector; hiện checklist chỉ ghi “every write”.
   - Review Focus 1 phải dùng ảnh flash sinh bằng **writer của revision trước 09b**, ghi rõ revision. Writer hiện tại đã gom text qua buffer và cắt theo byte tại 8 KB, không bảo đảm cắt cuối node. Kiểm cả GET nguyên dữ liệu và runtime tương đương sau nâng cấp, không chỉ `state == OK`.

Các ruling còn lại phù hợp với D1–D8. Việc dừng dịch vụ khi cấu hình quản trị sai, giữ Reload độc lập và không tự chạy bằng mặc định là đúng hướng đã chốt.