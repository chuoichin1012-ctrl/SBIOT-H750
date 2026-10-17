Plan 11a, vòng 7 (Task 0b). Đã sửa điểm của vòng 6:
- quyết định nằm trong `hb_restart_was_ours(por, bor, iwdg, pin)` của `hb.c`. Đây là hàm thuần: `!por && !bor && (iwdg || pin)`;
- có cờ power-on hoặc brown-out thì bỏ bản ghi, kể cả khi `PINRSTF` cũng bật;
- test (`test_board`): `PORRSTF|PINRSTF`, `BORRSTF|PINRSTF`, cả bốn cờ cùng bật, và một software reset đều không phải do watchdog; `IWDG1RSTF`, `PINRSTF`, hoặc cả hai, thì là do watchdog.

Plan đã được cập nhật đoạn này. Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa.
