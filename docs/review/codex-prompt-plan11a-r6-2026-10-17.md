Plan 11a, vòng 6 (Task 0b, đường lưu lý do reset). Đã sửa cả hai điểm của vòng 5:

1. **Cache:**
   - bản ghi lý do là một đối tượng 32 B, căn theo một cache line và nằm riêng trong line đó, trong section `.noinit` ở SRAM4;
   - mỗi lần ghi hoặc xóa đều có `SCB_CleanDCache_by_Addr` rồi `__DSB()`; lúc boot thì đọc sau `SCB_InvalidateDCache_by_Addr`;
   - gate mới trong `check_elf.py`: `.noinit` là NOLOAD, căn 32 B, nằm trong SRAM4, ngoài mọi vùng mà linker script của bootloader dùng, và startup không xóa nó.
2. **Thứ tự lúc boot:**
   - `app_main` chụp `RCC->RSR` vào một biến static trước khi bất cứ chỗ nào xóa nó;
   - sau khi `store_init` đã mount: có bản ghi hợp lệ, kèm `IWDG1RSTF` hoặc `PINRSTF` (trường hợp SGM706 reset qua NRST khi lắp R20), thì ghi log; có bản ghi mà chỉ có cờ power-on hoặc brown-out thì không ghi gì (RAM rác);
   - trong mọi trường hợp, bản ghi đều bị xóa (clean và DSB).

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa.
