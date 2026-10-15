Plan 06b đã code xong và chạy trên board. Branch `plan06b-web`. Trước khi tạo PR, xin **review code**:
- `git diff main..HEAD -- stm32/`;
- plan `stm32/docs/plans/2026-10-15-06b-web.md`: phát hiện B1–B6, đo đạc, 5 ruling đã duyệt;
- spec §6.4, §9.1 (giới hạn đã biết mới), §10.

**Trọng tâm:**
1. `components/webapi/web.c`:
   - các slot kết nối, `on_headers` (mỗi request một lần, B1);
   - `reject` + `ST_DISCARD` (B2);
   - upload theo luồng (OTA, cert, Edge) với `c->pfn = NULL`;
   - các câu trả lời có giới hạn bởi người đọc (file, GET Edge với pin/unpin, log đếm dòng);
   - `is_resp` (B3);
   - quy tắc 408;
   - arena (một scope trong một callback).
2. `api.c`: các endpoint, `/api/status.mem`, quy tắc áp dụng sau khởi động lại (`/api/device` trả giá trị đang chạy).
3. `console.c`:
   - kiểm auth trước upgrade;
   - 4 client;
   - MessageBuffer: chỉ task syslog ghi qua tap;
   - bỏ frame khi `c->send` > 4 KB.
4. `components/sysmgr`:
   - `clock.c` (`test_clock`);
   - offset 64 bit đọc ghi trong critical section;
   - SNTP dưới core lock, tên server tĩnh;
   - restart task.
5. `web_core.c` (`test_web_core`), arena theo task (`test_arena`, `app/arena_port.c`).
6. UI nhúng thành mảng C (B5).
7. **B4, lỗi bản ghi Edge 8 KB trên board bench:** nguyên nhân là partition từng được format với sector 4 KB. Không vá FlashDB (quy tắc của người dùng); đã ghi vào giới hạn đã biết. Xin cho ý kiến: có cần kiểm layout lúc mount không, hay chấp nhận như vậy?

**Trên board:**
- `web_check.py` 36/36;
- `npm run test:pages` lưu và tải lại 7 trang;
- `net_check.py` và `store_check.py` đạt.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa (lỗi thật, theo mức độ). Nhớ người dùng muốn giữ đơn giản.
