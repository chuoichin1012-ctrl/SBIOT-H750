Vòng 2 review code 06a: `git diff 5f67853..HEAD -- stm32/`. Hai lỗi Important của vòng 1 đã sửa, xem mục "Code review round 1" cuối plan.

1. **Đổi tốc độ hoặc duplex:** hàm thuần `ip101_action(applied, now)` trả keep, start, stop hoặc reconfigure.
   - Reconfigure là trường hợp vẫn up nhưng tốc độ hoặc duplex khác, ví dụ 10 → 100 mà không thấy down ở giữa.
   - Lỗi đọc thì giữ nguyên trạng thái.
   - Có test trong `test_ip101`, RED trước rồi mới GREEN.
   - `eth_drv`: stop, cấu hình lại MAC, start, tất cả dưới core lock; netif vẫn up.
2. **RNG:** thêm `components/board/rng_core.c`, đọc thanh ghi trực tiếp, không dùng `HAL_RNG_GenerateRandomNumber`.
   - CECS: word thất bại.
   - Seed error: phục hồi một lần theo RM0433 §34.3.7 (xóa SEIS, bỏ 12 word, SEIS phải vẫn sạch).
   - Word đọc trúng lúc lỗi xuất hiện, kể cả xuất hiện cùng DRDY, thì bị bỏ.
   - `board_rng` không ghi gì thêm vào buffer khi một word thất bại.
   - `test_rng` (RED trước) có 6 trường hợp, kể cả lỗi xuất hiện cùng DRDY.

Trên board:
- `net_check.py` đạt lại;
- không còn log "RNG FAILED". lwIP gọi `LWIP_RAND` lúc khởi động;
- `ota_check.py` đạt (regression), `store_check.py` đạt.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
