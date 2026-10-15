Code 07b đã được duyệt ở review r2 (`f1929d1`). Sau đó chỉ có: đo ADC trên board có VREF, một trace AI cho bench, và ca `adccheck` trong `io_check.py`. Xin xác nhận lần cuối trước PR: `git diff f1929d1..HEAD -- stm32/`.

- `iomgr.c`: khi bật trace bench (`D`), mỗi cửa sổ AI được công bố ghi một dòng `io: AI <µA> <µA> at <ms>` (mặc định tắt).
- **Kết quả đo** (mục Measurements của plan):
  - hiệu chuẩn hai điểm; 4/12/20 mA sai số ≤ 0,006 mA, dao động ≤ 0,0006 mA;
  - các cửa sổ đúng 100 ms (1 787 cửa sổ liên tiếp);
  - nguồn chỉ đổi dần (khoảng 4 mA/s), nên độ trễ được chứng minh theo thiết kế (≤ khoảng 200 ms), còn bậc thật để Plan 11.
- **Không làm** (người dùng quyết định): ảnh hưởng chéo khi AI2 hở mạch (Plan 11, P1); kiểm hồi quy trên board mới (đã đạt trên board trước với cùng image: serial 35/35, web 44/44, store).
- Host test `test_io_core`, `test_adc_core`, `test_reload`, `test_serial_frame` GREEN; `check_elf` đạt.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi phải sửa.
