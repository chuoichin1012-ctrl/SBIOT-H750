Vòng 3 cho kế hoạch 07a. Đã sửa ca bench XOFF/XON theo vòng 2:
- trên RS485 PC gửi XOFF *trước* khi yêu cầu gateway phát, rồi kiểm XON, timeout và lần gửi kế tiếp;
- việc dừng giữa lúc DMA đang chạy được kiểm bằng fault injection trên board (lệnh `X`): XOFF 20 ms sau khi bắt đầu, XON 1 s sau đó, PC phải nhận đủ, đúng thứ tự, có khoảng im lặng khoảng 1 s;
- ghi rõ chưa kiểm XOFF nhận trong lúc TX (cần RS232).

Xem mục "Codex round 2" cuối plan.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách việc phải sửa.
