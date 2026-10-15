Đã sửa 6 lỗi Important và lỗi Minor của review code 07a vòng 1 (commit mới nhất trên `plan07-serial-io`). Chi tiết ở mục "Code review round 1" cuối plan `stm32/docs/plans/2026-10-15-07a-serial.md`. Xin **review lại**: `git diff 53136b6..HEAD -- stm32/`.

1. `sf_ring_take` (serial_frame): mỗi event được đối chiếu với vị trí DMA **hiện tại**; một lap (hoặc cách lap dưới 64 byte) là overrun, reader nhảy tới vị trí DMA; event nằm sau reader thì không đọc gì. `serial.c` dùng nó trong `rx_task`; sau `lost`, reader nhảy tới vị trí DMA hiện tại.
2. `sf_rx_mode` giữ `resync` khi rời RTU (vào RTU thì xóa, vì transaction luôn bắt đầu sau khoảng im lặng); `serial_transact` chờ trong RTU, tối đa 200 ms, cho `resync` hết sau một câu trả lời bị bỏ.
3. ISR đăng ERROR trước GAP; `rx_task` đọc tới byte trước byte lỗi, xử lý lỗi, rồi mới đọc byte lỗi. Lỗi đường truyền giờ bỏ cả frame tới khoảng im lặng kế tiếp (trước đây phần đuôi thành frame).
4. `rx_quiet_since`: cập nhật dấu thời gian khi vị trí DMA đổi hoặc USART BUSY; dùng cho collision avoidance và cho vòng chờ trước transaction. Ca bench mới (`serial_check.py collision`) fail trước khi sửa, pass sau khi sửa.
5. `sf_t35_oneshot_us` = t3.5 − thời gian RTO đã lập trình (số bit nguyên); test RTO + one-shot = t3.5 từ 600 tới 230 400 baud.
6. `flash_spi4_wipe`: khóa (đệ quy) rồi seal chip, mọi lệnh ghi qua `flash_spi4()` bị lỗi từ đó, rồi xóa. Bench: `Z WIPE` giữa lúc upload OTA web; upload bị cắt, lần boot sau không cài gì (chỉ `seeded-factory`).
7. Minor: `serial_check` kiểm flood thật sự chạy (số gói > 10 000).

**Kết quả:** `test_serial_frame` GREEN; trên board `serial_check.py` 30/30, `web_check.py` 42/42, `store_check.py` đạt, `check_elf.py` đạt.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa. Nhớ người dùng muốn giữ đơn giản.
