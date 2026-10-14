Vòng 2 của Plan 03, tiếp tục trong cùng phiên. Plan đã được viết lại theo quyết định NOT APPROVED của bạn: `stm32/docs/plans/2026-10-14-03-edge-pipeline.md` (commit `428683e`).

Cách xử lý từng điểm:
1. **OOM:**
   - Mọi lần hook của cJSON trả NULL đều được đếm (`json_alloc_failures()`, có bản weak mặc định trong cfgmodel). Cả hai entry point so bộ đếm trước và sau; `cfg_edge_text` còn kiểm ngay sau mỗi object mới, trước khi đọc bất kỳ trường nào.
   - `take()`/`object()`/`list()` giờ giải phóng item khi không đặt được nó vào chỗ. Đây chỉ là xử lý cho trường hợp cấp phát thất bại, đường chạy thành công không đổi.
   - Có `test_oom.c` mới: lần lượt làm hỏng **từng** lần cấp phát, khoảng 1.800 lần qua 5 input. Lần nào cũng phải ra `CFG_NO_MEMORY`, không crash, không leak.
   - Fault injection đã bắt được và sửa 3 lỗi thật: deref NULL ở `source`, leak trong `pick()`, leak trong `put_field`/`list()`.
   - UBSan giờ dừng chương trình khi phát hiện lỗi (`-fno-sanitize-recover=all`).
2. **Buffer in:** `CFG_SMALL_BUF` = `CFG_SMALL_MAX` + 6, giới hạn được kiểm trên `strlen`. Có test output dài đúng 10.239, 10.240 và 10.241 byte.
3. **Unicode:** scanner kiểm UTF-8 theo RFC 3629 (không overlong, không surrogate, không vượt U+10FFFF) và surrogate escape phải đi theo cặp như cJSON yêu cầu. Có test cho cả key, value và cả member bị bỏ qua. Spec §9.4 được cập nhật về miền JSON và giới hạn độ sâu 32.
4. **emit=false:** dừng ngay (`goto aborted`). Có test: sink từ chối ở lần ghi thứ 3, phía sau còn một lỗi schema, kết quả vẫn phải là `CFG_ABORTED` và không còn lần gọi emit nào sau đó.
5. **Bộ nhớ:** mọi test cấu hình nhỏ chạy trong scope arena thật (pool TLSF 64 KB). Với từng cấu hình, test tự dò tài liệu lớn nhất (mỗi chuỗi dài tối đa, mỗi danh sách dài tối đa), kiểm round-trip và kết quả 413. Đỉnh lớn nhất trên host 64-bit là 41.832 byte.
6. **Bước thực thi:** RED của Task 1/2 giờ ghi đúng là "No rule to make target". Bước mutation là Step 6 của Task 4 (đã đổi thứ tự commit). Có 4 mutant, kể cả mutant bỏ phép kiểm sau `object()`, bị UBSan bắt.

Toàn bộ plan đã được **mô phỏng từng task trên một bản export sạch** của repo: RED và GREEN đúng như ghi trong plan, và file cuối giống hệt bản thử. Kết quả: 7 bộ test đạt, 8.090 payload tương đương.

CHỈ đọc, trả lời ngắn bằng tiếng Việt. Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`, dòng thứ hai là `THỰC THI: <SUBAGENT | NATIVE>`.
