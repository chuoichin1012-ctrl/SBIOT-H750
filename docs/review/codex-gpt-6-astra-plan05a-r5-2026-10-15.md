QUYẾT ĐỊNH: NOT APPROVED

Hai lỗi vòng trước đã được xử lý. Còn **1 Important trong phạm vi chịu lỗi vừa chốt**:

- **Lỗi ghi/xóa dai dẳng chưa đưa storage sang chỉ đọc** — [fal_nor.c:43](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/fal_nor.c:43), dòng 55. `program`/`erase` lỗi chỉ trả `-1`, không bật khóa. Nếu đọc vẫn tốt nhưng ghi liên tục lỗi, các yêu cầu lưu sau vẫn tiếp tục xuống flash; vùng khác còn ghi được vẫn có thể bị thay đổi. Điều này trái §9.1. Cần quy tắc hữu hạn để khóa ghi/xóa, từ chối lưu và báo lỗi; thêm test lỗi program/erase dai dẳng, xác nhận không còn thao tác ghi/xóa sau khi khóa.

**Giới hạn đã biết:** không yêu cầu tự phục hồi phần cứng hoặc tiếp tục vận hành sau lỗi dai dẳng. Nghiệm thu SPI4 và mất nguồn trên board thuộc Plan 05b, chưa được chứng minh bằng kiểm thử host.

Hai binary có sẵn `test_store` và `test_store_small` chạy đạt khi tắt LeakSanitizer. Không rebuild hoặc chạy lại ma trận/mutant; phát hiện trên dựa trên đọc mã.