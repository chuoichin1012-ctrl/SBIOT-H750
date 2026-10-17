NOT APPROVED

Còn một điểm: **các cờ reset không loại trừ nhau**. `PINRSTF` cũng bằng 1 sau power-on reset, nên điều kiện hiện tại có thể nhận bản ghi còn sót là watchdog reset. [RM0433, thanh ghi RCC_RSR](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

Sửa thứ tự quyết định:

1. Có cờ power-on hoặc brown-out → bỏ bản ghi, **kể cả khi `PINRSTF` cũng bật**.
2. Nếu không, bản ghi hợp lệ và có `IWDG1RSTF` hoặc `PINRSTF` → ghi log.
3. Luôn xóa bản ghi như đã mô tả.

Thêm test cờ kết hợp `PORRSTF | PINRSTF` và `BORRSTF | PINRSTF`, với bản ghi CRC hợp lệ: không ghi lý do watchdog. Các bổ sung về cache và thứ tự khởi động đã đạt.