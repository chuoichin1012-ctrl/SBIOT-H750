Đã sửa 3 lỗi Important của review code 07a vòng 2 (commit mới nhất trên `plan07-serial-io`). Chi tiết ở mục "Code review round 2" cuối plan `stm32/docs/plans/2026-10-15-07a-serial.md`. Xin **review lại**: `git diff dd0fd7f..HEAD -- stm32/`.

1. Bỏ mốc 200 ms. Sau câu trả lời bị bỏ, port giữ timing RTU (bus đã nhả); `serial_rx[n]` chuyển về transparent khi t3.5 im lặng kết thúc resync (`tail`, dưới `txn_lock`; transaction mới lấy lại mode trước). Ca bench `tail` (1200 8E1, `packetInterval` 1, đuôi 412 ms, nghỉ 20 ms, thêm 3 byte): fail trước khi sửa (3 byte lọt ra), pass sau khi sửa.
2. Cả ngắt DMA lẫn ngắt USART đăng ERROR trước (`rx_errors`), vì DMA chỉ chuyển byte sau khi USART đã đặt cờ lỗi. `sf_rx_ring` (logic đọc ring theo event, giờ có host test) bỏ mọi byte kể từ lần đọc trước khi gặp ERROR, cùng frame của chúng, tới khoảng im lặng kế tiếp; có test byte lỗi không phải byte cuối.
3. `sysmgr_restart_claim`: người gọi đầu tiên sở hữu lần restart duy nhất. `Z` giành nó trước, bị từ chối khi đã có restart chờ; trong lúc wipe, mọi restart khác đều bị bỏ qua. Bench: OTA xong rồi `Z WIPE` ngay: bị từ chối, restart của OTA vẫn chạy.

**Kết quả:** `test_serial_frame` GREEN; trên board `serial_check.py` 32/32, `store_check.py` đạt, `check_elf.py` đạt. `web_check.py` một lần fail vì timeout kết nối ngay sau ca 30 s im lặng (board vẫn chạy, ping được), sau đó đạt 42/42 hai lần, một lần có ghi console (không fault, không reset bất thường).

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa. Nhớ người dùng muốn giữ đơn giản.
