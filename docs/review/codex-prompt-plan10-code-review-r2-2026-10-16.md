Review code Plan 10, lượt 2: sửa 5 điểm Important của lượt 1 (`codex-gpt-6-astra-plan10-code-review-2026-10-16.md`), commit 2fc6fd8.

1. **TX làm tràn RX:** mỗi vòng `cmux_rx` chỉ gửi một frame (tối đa 2 055 byte, 178 ms ở 115200), hoặc 1 KB ở PPP thẳng, rồi rút RX trước khi gửi frame kế tiếp. Ring 4 KB chứa được 350 ms nên không thể bị vượt vòng.
   - Ruling: chưa có test tải hai chiều, vì board này không có dây nối UART1↔UART2.
   - Chạy lại `load` chiều xuống: 1 091 000 byte, overrun 0, bad FCS 0.
2. **NO SIM chặn phục hồi:** `lte_supervise` trả RECOVER khi modem im lặng 60 s và AT không trả lời, kể cả khi NO SIM. Test đỏ trước, xanh sau.
3. **Shutdown trong PPP thẳng:**
   - trước `QPOWD`: chuyển về AT, chờ 1 s, gửi `+++`, chờ 1 s, gửi `ATH`;
   - lệnh bench `j` ép đường dự phòng (`AT+CMUX=9`, bị từ chối);
   - ca `lte_check fallback` đạt: PPP lên trên UART thường, và restart nhận được `POWERED DOWN`.
   - Trong lúc làm, phát hiện đường PPP thẳng quay số khi module vừa reset, chưa đăng ký mạng. Giờ nó chờ đăng ký (tối đa 90 s), như đường CMUX.
4. **Mất URC trong buffer:** `at_cmd` và `at_wait` đọc từng byte, nên không lấy gì sau dòng kết quả cuối; `POWERED DOWN` đến sau `OK` vẫn còn cho `at_wait`.
   - Ruling: bộ đọc là code board; `test_at_parse` kiểm `OK` và `POWERED DOWN` trong cùng một đoạn cho ra hai dòng.
5. **Ping:** một gói chỉ được tính khi qua `inet_reply_ok`, là hàm thuần có test (`test_netcfg test_reply_ok`, đỏ trước, xanh sau). Điều kiện:
   - là echo reply, đúng ID, checksum hợp lệ, đúng vòng probe;
   - nguồn đúng là target mà sequence chỉ ra;
   - nhận trên đúng link mà sequence chỉ ra (`ip_current_input_netif`).

**Kết quả:** host `test_lte_power`, `test_netcfg`, `test_at_parse`, `test_cmux` đều xanh; `check_elf` đạt. Trên board, `lte_check` fallback, mux, load và power đạt.

**Phạm vi:** `git diff 994069a HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi).
