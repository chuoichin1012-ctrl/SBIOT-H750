NOT APPROVED

Cần sửa các điểm sau trong plan:

1. **Task 0 — nhịp task không đủ để tạo nhịp LED đã yêu cầu.** Task chạy mỗi 100 ms không tạo được các lần đảo cách nhau 250 ms đều đặn. Dùng bước 50 ms là cách đơn giản. Thêm test với đúng nhịp gọi thực tế. Đồng thời phân biệt khoảng cách giữa các lần nạp IWDG và đảo WDI: WDI bình thường cách nhau 500 ms, nên không thể giữ tiêu chí **WDI < 400 ms** bằng cách chỉ đo vòng task.

2. **Task 2 — bộ đếm chưa bảo đảm không cấp trùng SN.** Hai tiến trình có thể cùng đọc một số; rename cũng chưa bảo đảm dữ liệu đã bền trước khi in SN. Khóa toàn bộ thao tác đọc–tăng–ghi bằng một lock file riêng; ghi file tạm, flush/fsync, rename và fsync thư mục trước khi in. Thêm test chạy đồng thời và lỗi ghi: lỗi thì không xuất SN. [Python `fsync`](https://docs.python.org/3.11/library/os.html#os.fsync), [đồng bộ thư mục](https://man7.org/linux/man-pages/man2/fsync.2.html).

3. **Task 1 — quy tắc UID chưa khớp test.** Chữ ký được kiểm bằng **UID của chip**, còn UID trong record được mô tả là “for people reading it”. Vì vậy chỉ sửa UID lưu trong record rồi tính lại CRC sẽ không làm chữ ký sai. Nếu yêu cầu kết quả `BIND_CLONE`, phải ghi rõ bước so UID trong record với UID chip.

4. **Task 4 — chưa có đường đọc lại hệ số vừa ghi.** Hiện `a` chỉ báo ADC và cờ calibration của runtime; lệnh `A` chỉ áp dụng sau reboot. Bắt buộc bổ sung đọc lại hai bản factory record từ flash, kiểm CRC và so hệ số với giá trị yêu cầu. Kiểm `aiCalibrated` sau reboot, không dùng trạng thái cũ để kết luận ghi thành công.

5. **Task 6 — thiếu quy trình chuyển các image cũ sang v2.** Staging cũ còn hợp lệ có thể ghi đè app v2 vừa nạp; factory cũ vẫn được giữ vì đã có magic. Sau khi bind v2, các app cũ đó từ chối record mới và dừng. Cần quy trình chuyển đổi một lần để xử lý staging và cập nhật factory image tương thích v2; kiểm cả boot thường lẫn Reload restore. Không cần thêm cơ chế tự di trú trong bootloader.