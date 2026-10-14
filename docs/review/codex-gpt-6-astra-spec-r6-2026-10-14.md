QUYẾT ĐỊNH: APPROVED WITH CONDITIONS

Đã kiểm commit `9eb71bf`. **Hai lỗ hổng vòng 5 đã được xử lý:** hàng “không còn nguồn hợp lệ” được ưu tiên; đường RAM đã có bộ đếm, deadline, kiểm nguồn và nhánh thất bại.

Tuy nhiên, còn **một giả định sai trong thủ tục RAM**:

> “QSPI đang dở nên không có `Vq`.”

Trạng thái `WRITE`/`RESTORE` không chứng minh image QSPI không hợp lệ. Ví dụ: đã ghi xong checkpoint cuối nhưng ghi record cập nhật thất bại; QSPI có thể đã chứa image hoàn chỉnh. Hàng 1 chỉ bảo đảm `Vq ∨ Vb ∨ Vs`, **không bảo đảm `Vb ∨ Vs`** như bước 3 đang suy ra. [§11.2, hàng 4–6 và bước 3](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:878).

**Điều kiện sửa spec trước khi chốt kế hoạch:**

- Xử lý riêng `Vq=true` khi ghi record thất bại, kể cả trạng thái còn là WRITE/RESTORE.
- Chỉ vào đường phục hồi RAM cần nguồn ngoài khi `Vq=false` và còn `Vb` hoặc `Vs`.
- Thêm test: ghi xong image nhưng cập nhật record thất bại; gồm trường hợp **chỉ QSPI còn hợp lệ**, bảo đảm không xóa image đó hoặc dừng hẳn vì thiếu backup/staging.

Được tiếp tục lập kế hoạch. **P1–P3 giữ nguyên:** RAM/pool lwIP; scheduling, backpressure và ADC; factory record, loader SPI4 và handoff. Các gate OTA, modem ON/OFF/UNKNOWN và PPP giữa frame vẫn bắt buộc.

Chỉ đọc; chưa xác nhận PASS firmware/HIL.