QUYẾT ĐỊNH: APPROVED WITH CONDITIONS

Đã kiểm diff tới `69e2b1b`. **S6, S7 và diễn giải PPP đạt; S1 còn hai lỗ hổng trong bảng quyết định mới.**

| Mục | Kết quả |
|---|---|
| **S1 — OTA** | **Chưa đạt**, chi tiết dưới đây. |
| **S6 — modem** | **Đạt ở mức spec**. Trạng thái UNKNOWN được giữ đúng; giả định xung 2,5 s không tắt modem đang ON phải được xác nhận trên bench với firmware module thực dùng. |
| **S7 — RAM** | **Đạt**. Ngoại lệ cấp từ heap đã rõ. |
| **PPP** | **Đạt**. Đã mô tả callback nhiều đoạn và bổ sung test lỗi từ callback thứ hai. |

**Hai điểm phải sửa trong §11.2 trước khi chốt kế hoạch:**

1. **Hàng 8 che hàng 9.** Ví dụ: WRITE cạn lượt thử, cả QSPI, backup và staging đều hỏng. Theo quy tắc xét từ trên xuống, hàng 8 chọn *cứu hộ có thử lại*, không tới hàng 9.  
   **Sửa:** kiểm “không còn nguồn hợp lệ” trước các nhánh phục hồi, hoặc thêm điều kiện còn nguồn hợp lệ vào hàng 8. Thêm test đúng tổ hợp này. [Bảng quyết định](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:881).

2. **Hàng 5 mới chỉ mô tả đường thành công.** Chưa xác định hành động khi không tìm được nguồn hợp lệ, chép lỗi hoặc SHA đích không khớp trong chế độ không checkpoint. Hàng 1 cũng nói lần thử “đã được đếm trong `attempts`”, điều không được bảo đảm trên đường này vì ghi record đang thất bại.  
   **Sửa:** quy định rõ deadline và nhánh lỗi riêng cho phục hồi bằng tiến độ RAM: còn nguồn hợp lệ thì cứu hộ có thử lại; hết nguồn thì dừng hẳn; tuyệt đối không nhảy khi kiểm image thất bại. Thêm fault injection cho timeout, sai SHA và mất nguồn trên chính đường này. [Hàng 1–5](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:875).

Đây là lỗi phát sinh từ cách hợp nhất bảng và đường phục hồi mới, không phải chi tiết cần hoàn thành bằng firmware trước khi lập kế hoạch.

**P1–P3 vẫn giữ nguyên:** ngân sách RAM/pool lwIP; scheduling, backpressure và ADC; factory record, loader SPI4 và handoff. Kế hoạch bổ sung các ca OTA trên, bench modem ON/OFF/UNKNOWN và PPP bị từ chối giữa frame.

Chỉ đọc; chưa có xác nhận PASS firmware/HIL.