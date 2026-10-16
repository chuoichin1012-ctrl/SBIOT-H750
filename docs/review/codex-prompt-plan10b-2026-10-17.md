Plan 10 đã merge (PR #21). Người dùng yêu cầu thêm hai cải tiến cho trường hợp lâu không có mạng LTE:
- **Giãn dần thời gian giữa các lần thử** thay vì CFUN/RESET mỗi 2,5 phút mãi mãi.
- **Power cycle mềm làm phương án cuối:** `QPOWD`, chờ xác nhận `POWERED DOWN`, rồi PWRKEY 2,5 s.

Plan: `stm32/docs/plans/2026-10-17-10b-lte-backoff.md`, branch `plan10b-lte-backoff`.

Tóm tắt:
- Thời gian chờ khi PPP không có địa chỉ: 2, 5, 10 rồi 30 phút, giữ 30 phút về sau.
- Hai lần đầu dùng CFUN như hiện nay; từ lần thứ ba dùng power cycle mềm. Không nhận `POWERED DOWN` thì quay về RECOVER bằng RESET_N, không bao giờ nhấn phím khi chưa biết trạng thái.
- Có địa chỉ thì đặt lại về 2 phút.
- Giữ nguyên: RECOVER khi modem im lặng, quy tắc NO SIM, không có xung PWRKEY nào từ 3 s trở lên.
- Phần logic là hàm thuần có test; bench chạy với timing rút ngắn bằng lệnh console `b`.

Cần duyệt các ruling 1–4 và hai task. Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa. Nhớ là người dùng muốn giữ đơn giản.
