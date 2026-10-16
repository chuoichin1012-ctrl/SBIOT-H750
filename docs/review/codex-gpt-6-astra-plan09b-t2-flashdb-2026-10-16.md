**Tôi chọn cách khác: FAL khóa ghi/xóa ngay khi một `program` hoặc `erase` thất bại, cho tới reboot.** Đây là cách đơn giản hơn retry và không cần vá FlashDB.

Theo trace của bạn, khóa này chặn ngay bước 2: FlashDB không thể ghi record vào sector vẫn mang trạng thái `empty`, cũng không thể xóa nguồn trong lần mount lại. Cần ma trận kiểm chứng kết quả sau reboot, chưa nên coi đây là bảo đảm cho mọi đường lỗi.

Cụ thể:

1. FAL đặt cờ lỗi ghi **trước khi trả lỗi cho FlashDB**; từ đó từ chối mọi program/erase. Không xóa cờ khi mount lại.
2. Store phát hiện cờ này thì trả `STORE_EFLASH`, không commit hay dọn record; Edge chuyển sang `UNAVAILABLE`, **không trả `[]`**.
3. Giữ bộ đếm lỗi để phát hiện trường hợp FlashDB nuốt lỗi. Khi đã khóa, không cố khôi phục bằng một lần mount có ghi trong cùng boot.
4. Giữ nguyên ma trận **một lần lỗi**, thêm lỗi kéo dài và ghi/xóa dở. Sau reboot khi bỏ fault, model phải là đúng bản cũ hoặc bản mới đã commit; riêng op 2 phải giữ bản cũ.

**Không chấp nhận (a) như đang mô tả:** ba lần thất bại không miễn yêu cầu “không làm hỏng thêm”; xóa nguồn rồi mất manifest vẫn vi phạm. Retry cũng cần xử lý trạng thái chip: [driver hiện tại](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/board/spi_nor.c:106) có thể báo lỗi sau khi đã phát lệnh, nên tính chất NOR AND chưa đủ chứng minh retry an toàn.

Không chọn (b): “sector empty có dữ liệu” chưa đủ chứng minh dữ liệu đó chưa commit. Không chọn (c), vì có thể chặn việc ghi tiếp ở FAL mà không mở rộng bản vá FlashDB.