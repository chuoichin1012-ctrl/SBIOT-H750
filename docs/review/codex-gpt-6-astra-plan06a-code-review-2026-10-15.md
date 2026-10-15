NOT APPROVED

Có **2 lỗi Important** cần sửa tại `5f67853`:

1. **[eth_drv.c:187](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/eth_drv.c:187) — bỏ sót thay đổi tốc độ/duplex.**  
   Nếu PHY thương lượng lại giữa hai lần poll, cả hai lần đều thấy `up`, code trả về ngay dù link đã đổi 10→100 Mbit hoặc duplex. MAC giữ cấu hình cũ, có thể mất kết nối.  
   **Sửa:** so sánh cả tốc độ/duplex; khi đổi, stop → cấu hình lại → start dưới core lock. Thêm test `up 10 → up 100` không có lần quan sát `down`.

2. **[board_rng.c:31](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/board/board_rng.c:31) — `HAL_OK` chưa bảo đảm dữ liệu RNG hợp lệ trên H750.**  
   HAL vendored bỏ kiểm seed error khi MCU không có `RNG_CR_CONDRST`, như H750. Vì vậy có thể trả thành công dữ liệu không đủ entropy; `DeInit/Init` cũng chưa thực hiện chuỗi phục hồi seed error của RM0433.  
   **Sửa:** kiểm trạng thái lỗi theo [RM0433 §34.3.5–§34.3.7](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf), phục hồi đúng hoặc trả lỗi; chỉ chép vào output khi `word()` thành công. Thêm fault test khi lỗi xuất hiện cùng `DRDY`.

Các ruling 1–5 chấp nhận được. `MEM_SIZE + 64` đủ với cấu hình hiện tại; chưa thấy lỗi chặn khác ở pool, vòng đời buffer hay patch DNS.

Tôi chạy lại **ba binary host có sẵn** (`test_ip101`, `test_netcfg`, `test_dhcp_dns`): đều đạt. Không rebuild hoặc chạy lại board trong lượt review này.