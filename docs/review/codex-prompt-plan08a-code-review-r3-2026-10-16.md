Đã sửa lỗi kiểm thử của vòng 2: ca `stuckpair` (`bridge_check.py`, `case_tcpserver`) lại kiểm tốc độ sau phục hồi:
- client khỏe phải nhận tiếp ít nhất 15 s sau khoảng chờ;
- trong thời gian đó phải nhận ≥ 90 % (tốc độ stream × thời gian), với tốc độ lấy từ dòng `E2 streamed` của board.

Kịch bản "1 byte rồi im" giờ fail. Trên board: chờ 10,9 s một lần, sau đó nhận 231 836 / 227 324 byte trong 19,8 s.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
