Tiếp tục trong cùng phiên. Plan 01 đã merge (PR #1 trên `SBIOT-H750`). Tôi đã viết **Plan 03**, chạy được trên host, theo roadmap: `stm32/docs/plans/2026-10-14-03-edge-pipeline.md`.

Toàn bộ code và test trong plan đã được chạy thử trong một bản sao ở scratchpad. Kết quả:
- 6 bộ host test đều đạt;
- `edge_text` so sánh **8 086 payload** (các ca cố định cộng 4 000 mô hình ngẫu nhiên) với `cfg_validate`/`cfg_load` nguyên khối mà không có khác biệt nào;
- 6 mutant (gồm cả lỗi khớp key `nodes`, vòng kiểm trùng slave ID, vòng kiểm chồng thanh ghi, rò rỉ input) đều bị bắt;
- đỉnh arena trong pool TLSF thật 64 KB, trên host 64-bit (khắt khe hơn MCU 32-bit): 15,7 KB với mô hình Edge lớn nhất, 41,8 KB với 16 topic đầy đủ.

Các quyết định thiết kế cần bạn xét:
1. Một entry point duy nhất `cfg_edge_text` chạy 3 pass, thay cho API `begin/device/node/end`. Lý do: toàn bộ văn bản đã nằm sẵn trong RAM.
2. `pick()` chỉ parse các key có trong schema. Key không biết thì bỏ qua mà không parse, nên không tốn bộ nhớ.
3. Scanner chặt theo RFC 8259, giới hạn độ sâu 32. Bản ESP32 thì chấp nhận cả những JSON mà cJSON khoan dung, như `01` hay rác ở cuối. Phép so sánh tương đương chỉ áp dụng trên miền JSON hợp lệ theo RFC.
4. `cfg_validate_text` giải phóng cây input trước khi in, và in vào buffer cấp sẵn (`cJSON_PrintPreallocated`). Không làm hai việc này thì `mqttPublish` cỡ lớn nhất bị phân mảnh và hết bộ nhớ trong pool 64 KB, dù tổng số byte đang dùng chỉ khoảng 48 KB.
5. Kiểm trùng tên bằng so sánh tuyến tính (`strcmp`), không dùng hash, vì chỉ có tối đa 100 điểm. Spec được sửa theo ở Task 4.

Thông tin thêm, không cần duyệt: đã cài toolchain cho Plan 02 (xPack gcc 15.2.1 và OpenOCD 0.12.0-7 trong WSL). Board thật đã kết nối qua STLINK-V3S, MCU là STM32H750 Rev V. COM5 là UART7.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt:
- Plan 03 có bước nào sai, mơ hồ, hoặc lệch spec không? Có thiếu ca test nào quan trọng không?
- Năm quyết định trên có chấp nhận được không?
- Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`, dòng thứ hai là `THỰC THI: <SUBAGENT | NATIVE>`.
