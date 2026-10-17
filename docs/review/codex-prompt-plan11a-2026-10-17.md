Plan 11a: `stm32/docs/plans/2026-10-17-11a-identity-and-gates.md`. Roadmap ghi Plan 11 là một plan, nhưng nay được chia làm hai: 11a (firmware và tool, test được trên host) và 11b (chạy bench HIL), vì bench hiện chưa có thiết bị đo.

**Quyết định của người dùng (2026-10-17):**
- **MAC:** dạng locally administered (`02` + SHA-256(UID)[0..4]), do tool tính;
- **SN:** `SB` + YYMM + 6 chữ số (bộ đếm trên máy nạp);
- **bản ghi cũ không có MAC/SN:** firmware dừng và báo lỗi, không tính lại từ UID;
- **LED WORK (cũng là WDI):** chu kỳ 1 s khi chạy, 0,2 s khi OTA/cài image/restore, 0,5 s khi dừng do lỗi.

**Chọn lựa chính:**
- **bản ghi liên kết v2** `SBB2` (104 B): chữ ký phủ cả UID, MAC và SN. Bản ghi v1 cho kết quả `BIND_EOLD`, dừng và không xóa gì;
- `provision.py next-sn` ghi bộ đếm trước khi in ra SN, nên một lần nạp hỏng chỉ bỏ phí một số, không bao giờ dùng lại số đó;
- **factory record** ghi qua lệnh console `A` trên UART7, vì loader SWD của nhóm chỉ ghi được QSPI;
- `audit_keys.py` được chuyển từ bản ESP32 sang;
- **Task 0:** một hàm thuần `pace_due` quyết định nhịp đảo WDI; IWDG1 vẫn được nạp lại ở mọi lần kick; task `wdog` chạy mỗi 100 ms.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa trong plan. Người dùng muốn giữ đơn giản.
