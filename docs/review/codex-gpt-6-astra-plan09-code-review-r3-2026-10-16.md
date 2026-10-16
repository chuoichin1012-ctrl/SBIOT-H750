APPROVED

Đã kiểm commit `3735c85`: cả ba lỗi r2 được xử lý. Mutex không còn làm lệch trạng thái coreMQTT/outbox; ID chờ SUBACK được giữ, tránh cấp trùng và giải phóng đúng vòng đời kết nối.

Không phát hiện lỗi Critical/Important mới trong phạm vi sửa. Không chạy lại host test hoặc bench trong lượt này.