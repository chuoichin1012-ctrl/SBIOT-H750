QUYẾT ĐỊNH: NOT APPROVED

Không thấy lỗi Critical; còn hai lỗi Important:

- **Upload thiếu vẫn có thể được cài:** [ota_app.c:33](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/ota/ota_app.c:33) ghi magic ngay từ đầu. Nếu staging cũ có phần đuôi giống image mới, upload dừng giữa chừng vẫn để lại image mới hợp lệ; `finish()` trả `OTA_ESHORT` nhưng boot sau cài nó. Trái §11.3. **Sửa:** chỉ ghi magic sau khi nhận đủ, kiểm SHA và đọc lại thành công. Thêm test staging cũ có cùng phần đuôi, upload thiếu rồi reset.

- **Gieo factory lỗi im lặng có thể khóa vĩnh viễn slot hỏng:** [install.c:49](/home/linh/projects/SBIOT-Gateway/stm32/firmware/boot/install.c:49) ghi magic trước khi kiểm đích. Một page body ghi sai nhưng báo thành công khiến factory có magic, SHA sai; các boot sau không gieo lại, Reload không dùng được. **Sửa:** kiểm nội dung đích trước khi ghi magic; thêm test `SIM_SILENT` khi gieo factory, bỏ fault rồi boot lại.

**Mâu thuẫn spec/test:** [§12.1:920](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:920) hứa sau mất điện khi restore chỉ dừng hoặc chạy factory, không cài staging. Nhưng mất điện ngay khi bắt đầu xóa staging có thể giữ nguyên Q và S; nếu S khác Q, boot thường vẫn cài S. [Test hiện tại:492](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/host/test_install.c:492) dùng S = Q nên che trường hợp này. Cần giới hạn lời hứa **sau khi staging đã vô hiệu hóa thành công**, thêm ca S ≠ Q và xóa chưa thay đổi byte nào; trước mốc đó người dùng phải giữ Reload lại.

**Năm ruling:** chấp nhận hướng thiết kế của cả năm; ruling 2/5 cần sửa cách xác nhận factory nêu trên, ruling 4 cần làm rõ ranh giới mất điện. Ruling 3 cần giữ yêu cầu tương thích cấu hình trong Plan 05.

Không thấy tham chiếu thiết kế cũ còn hiệu lực trong spec/roadmap; `handoff` kiểm bàn giao phần cứng và backup SRAM cho HardFault không phải cơ chế OTA đã bỏ.

Đã chạy binary `test_install` ở chế độ quick: đạt. Chưa chạy lại toàn bộ ma trận và mutants.