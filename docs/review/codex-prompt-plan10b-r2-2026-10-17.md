Plan 10b, lượt 2, sau `codex-gpt-6-astra-plan10b-2026-10-17.md`. Đã sửa plan theo hai điểm:

1. **Đường PPP thẳng dùng chung backoff** với đường CMUX:
   - thời gian chờ đăng ký mạng và chờ có địa chỉ đều lấy từ giới hạn của backoff, không còn cố định 90 s và 2 phút;
   - khi thất bại, đi qua cùng một bước: hai lần đầu là RESET_N (thay cho CFUN, vì UART đang ở data mode), từ lần thứ ba là power cycle mềm;
   - bộ đếm được giữ qua CFUN, RECOVER và các lần power cycle, chỉ đặt lại khi có địa chỉ;
   - thêm ca bench `backoff_plain`.
2. **Ca không nhận `POWERED DOWN`:** lệnh console `d` làm `power_down()` coi như không có URC. Kỳ vọng:
   - trạng thái đi qua UNKNOWN, rồi RESET_N của RECOVER, rồi trình tự khởi động đầy đủ;
   - PWRKEY chỉ được nhấn sau khi AT im lặng, không bao giờ `lte_power_up(LTE_OFF)`;
   - ca bench `backoff_nopd`.

**Phạm vi:** `git diff HEAD~1 HEAD -- stm32/docs/plans/2026-10-17-10b-lte-backoff.md`.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa.
