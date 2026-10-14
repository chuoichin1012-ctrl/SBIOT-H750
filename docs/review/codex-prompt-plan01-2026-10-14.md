Tiếp tục trong cùng phiên. Spec đã được bạn APPROVED ở vòng 7. Bước tiếp theo là kế hoạch triển khai, đã commit:

- `stm32/docs/plans/2026-10-14-00-roadmap.md`: chia thành **11 sub-plan**. Mỗi sub-plan tự ra phần mềm chạy và kiểm được, và chỉ được viết đầy đủ khi đến lượt, dựa trên code lúc đó. Bảng trong roadmap ghi rõ P1, P2, P3 và các gate bắt buộc của vòng 7 nằm ở sub-plan nào.
- `stm32/docs/plans/2026-10-14-01-host-core-and-web.md`: Plan 01, gồm 3 task, không cần board:
  - chép `proto` (bỏ RFC 2217) và `cfgmodel` sang cây `stm32/`, kèm host test và cJSON vendored;
  - sửa `cfgmodel` cho đúng phạm vi STM32 theo TDD (2 UART, 2 DI/AI, bỏ telnet/rfc2217, 100 điểm);
  - chép UI sang `stm32/web` và sửa, đổi đích của `check_schema.py`.

  Mọi lệnh, đoạn code và output mong đợi trong Plan 01 **đã được chạy thử** trong scratchpad, gồm test đỏ rồi xanh, typecheck, build và `check_schema`.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt:
1. Roadmap: thứ tự và cách chia có hợp lý không? P1–P3 và các gate của vòng 7 đã được phân bổ đủ chưa? Có thiếu phần nào của spec không?
2. Plan 01: có bước nào sai, mơ hồ, hoặc thiếu so với spec không? Đặc biệt chú ý:
   - kiểm toàn vẹn cây ESP32 bằng snapshot sha256;
   - không commit thư mục `www` được sinh ra;
   - Review Focus.
3. Phương thức thực thi cho Plan 01, chọn một:
   - **Subagent-driven:** mỗi task một subagent kèm reviewer riêng;
   - **Native:** tôi tự làm cả 3 task, rồi một reviewer review toàn bộ ở cuối.

Dòng đầu tiên phải là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`, dòng thứ hai là `THỰC THI: <SUBAGENT | NATIVE>`.
