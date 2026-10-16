Plan 09d (300 điểm Edge) đã làm xong trên branch `plan09d-300-points`, từ ca61f4d (plan được duyệt) tới 74c2d8e. Đây là bước review code bắt buộc trước PR.

Plan: `stm32/docs/plans/2026-10-16-09d-300-points.md`. Các mục "Measurements" và "Bench findings" ghi số đo và những gì tìm được.

**Các commit:**
- T1 (0bb10fc):
  - `STORE_EDGE_RECORD` 16 000, `CFG_EDGE_MAX`/`WEB_EDGE_MAX` 96 000;
  - `test_store`: body 96 000 thành 6 record, 96 001 trả 413, record 8 KB của 09c đọc lại được rồi được ghi lại; ma trận cắt điện và ma trận lỗi ghi đơn chạy thêm biến thể 96 000 byte.
- T2 (4a8d588):
  - `MAX_NODES` 300 ở `cfgmodel` và UI;
  - UI `edgeBodyError` (body minified, byte UTF-8, tối đa 96 000) hiện ở tab Data acquisition và tắt nút Save;
  - test: `test_cfgmodel` 300/301, `test_edge_text` model lớn nhất 300 điểm (90 304 B), `rules.test`.
- **Ngoài plan** (3a433d3), `spi_nor.c` `wait_ready`:
  - code cũ đọc status rồi mới đọc đồng hồ. Task bị chiếm CPU giữa hai bước quá 6 ms (TLS handshake của task khác) thì báo timeout với một status "busy" đã cũ;
  - FAL sau đó khóa store tới lần boot sau: lưu Edge trả 500, chứng chỉ không đọc được nên TLS dừng. Lỗi hiện một lần dưới tải 300 điểm;
  - sửa: đọc đồng hồ trước status;
  - `test_spi_nor` (fake HAL) đỏ trước khi sửa;
  - bootloader dùng chung file này nhưng poll không bị chiếm CPU, nên không flash lại.
- T3 (74c2d8e):
  - `mem_check` dùng 300 điểm, template report 100 giá trị (kiểm tại broker), lưu 96 000 byte, `connect_retry`;
  - `web_check` edge chạy ở 300 điểm và 96 000 byte; `edge_check memory` ở 300 điểm;
  - hai lỗi bench có từ trước: ca review 1b lưu một `system` thiếu trường; `edge_check basic` đọc status sau khi đã đóng slave;
  - spec cập nhật.

**Kết quả:**
- **Host:** đều xanh:
  - `test_store` (cả hai pha; `STORE_CUTS` với lần lưu 96 000 byte có GC: 430 thao tác, cắt ở mọi thao tác);
  - `test_store_small`, `test_web_core`, `test_cfgmodel`, `test_edge_text`, `test_text_small`, `test_oom`, `test_edge_core`, `test_spi_nor`, `test_retry`;
  - `npm run test:unit` 9/9, `build:firmware`.
- App build được, `check_elf` đạt. AXI 490 688 / 524 288 B.
- **Board:**
  - `mem_check` đạt. Ở (e): heap free 133 704, thấp nhất 66 784, khối lớn nhất 114 024; mức heap ổn định trong 1,7 KB; 34 report, mỗi report 100 giá trị; 4/4 lần MQTT kết nối lại; ADC 9,78 cửa sổ/s; watchdog cách tối đa 200 ms.
  - `edge_check basic remote memory`, `web_check` (cả bộ, hai lần) và `test:pages` đều đạt.

**Phạm vi:** `git diff ca61f4d HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi). Nhớ là người dùng muốn giữ đơn giản.
