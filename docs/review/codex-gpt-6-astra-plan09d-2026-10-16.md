NOT APPROVED

Cần sửa **gate đo RAM ở T3**:

- `mem_check.py:57` hiện tạo template chứa mọi điểm. Khi nâng lên 300, riêng phần dự phòng đã là **300 × 242 = 72.600 byte**, vượt giới hạn 26.624 byte; firmware sẽ tắt report. Bench có thể đạt nhưng số đo thiếu tải report.
- Ghi rõ: model có đủ **300 điểm**, template chọn khoảng **100 điểm** trong giới hạn; kiểm Edge thực sự chạy và report thực sự tới peer. Đồng thời đổi body tải ở các scenario (d)/(e) từ 48 KB thành **96.000 byte**.

Các lựa chọn còn lại hợp lý:
- Record 16.000 byte vừa sector 16.384 byte, kể cả header, tên key và khoảng dự phòng của FlashDB.
- Hai thế hệ chiếm 12 sector dữ liệu; **4 sector còn lại phải tính cả metadata và GC**, không phải riêng GC. Các ma trận T1 là gate phù hợp để xác nhận.
- Đọc record 8 KB cũ, giữ report 26 KB và giới hạn khi GET ghim thế hệ đều chấp nhận được.