### Strengths

Đã review phạm vi `abfe21a..390883f` và kiểm đủ năm mục Focus:

- Fault injection **1.818 lần cấp phát** đều trả `CFG_NO_MEMORY`.
- Có kiểm biên output **10.239 / 10.240 / 10.241 byte**.
- Member rác **40 KB** được bỏ qua; arena đầy được báo lỗi.
- Có kiểm key theo cJSON, Unicode và JSON sai trong member bị bỏ qua.
- Store từ chối ở lần emit thứ ba trả `CFG_ABORTED`, không bị lỗi schema phía sau ghi đè.

Bảy binary host test hiện có chạy đạt, gồm **8.090 payload** tương đương; `check_schema.py` đạt. Lần chạy này phải tắt LeakSanitizer vì sandbox, nên **chưa xác nhận độc lập không leak**.

Chấp nhận hai `Ruling:`: bản quyền có trong `tlsf.h`; mutant `&& 0` giữ nhãn được sử dụng và kiểm đúng đường abort. Hai file TLSF khớp bản upstream tại commit đã ghim.

### Issues

#### Critical (Must Fix)

Không phát hiện.

#### Important (Should Fix)

- **[arena.c:71](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/arena/arena.c:71)** — `arena_owns()` dùng `<`/`>=` giữa con trỏ heap và con trỏ vùng arena, thuộc các object khác nhau. Đây là undefined behavior theo [C11 §6.5.8¶5](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf#page=113), ngay trên đường giải phóng heap được hỗ trợ. Việc chọn allocator để `free` vì thế không được chuẩn C bảo đảm; chưa quan sát thấy lỗi thực tế. **Sửa:** kiểm vùng bằng địa chỉ `uintptr_t`, xử lý `NULL`, tránh tràn phép cộng; kiểm thêm đầu/cuối vùng và con trỏ heap.

#### Minor (Nice to Have)

- **[cfgmodel.c:1005](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/cfgmodel/cfgmodel.c:1005)** — Phân biệt `CFG_INVALID` với `CFG_NO_MEMORY` bằng nội dung buffer lỗi. Với `err=NULL`, `errlen=0` hoặc buffer một byte, lỗi schema bị trả thành OOM dù không cấp phát thất bại. **Sửa:** xác định mã trả về độc lập với thông báo; dùng bộ đếm cấp phát để nhận diện OOM và thêm test các trường hợp này.

### Declined to judge

- Đồng thời giữa task và arena theo task: thuộc tích hợp FreeRTOS ở Plan 05.
- Commit/rollback cấu hình trên flash: thuộc phần store; ở đây chỉ xét hợp đồng callback.
- Stack, timing và tổng RAM trên MCU: cần build và đo trên target.
- Kết quả mutation thực tế: không chạy lại vì cần sửa mã; đã xét mutant và test tương ứng.
- Whitespace trong TLSF vendored: giữ nguyên upstream, không yêu cầu sửa.

### Assessment

**Ready to merge? With fixes.** Sửa phép kiểm ownership của arena trước khi merge. Phần pipeline và các ca kiểm trọng tâm phù hợp plan; nên sửa luôn việc mã lỗi phụ thuộc buffer thông báo.