Review code Plan 09c, lượt 2: các sửa cho 3 điểm Important của lượt 1 (`codex-gpt-6-astra-plan09c-code-review-2026-10-16.md`), commit 31d0dbc trên `plan09c-ram`.

**1. Rò heap khi dựng JSON gặp OOM:**
- `api.c` và `iomgr.c` có helper `push(arr, item)`. Helper này giải phóng item khi `cJSON_AddItemToArray` không gắn được nó, và được dùng ở `add_pool`, ports, uart và các mảng do/di/ai.
- Các hàm `cJSON_Add*ToObject` vốn đã tự xóa item khi thất bại.
- Ruling: chưa có host test lỗi cấp phát cho `/api/status` và `/api/io`. Hai file này chỉ build cho board, phụ thuộc Mongoose, FreeRTOS và driver; dựng stub cho chúng thì lớn hơn chính phần sửa. Nếu sai, cái giá là một lần rò khi OOM ở một đường chưa đọc tới.

**2. Phiên streaming sống qua reboot giả lập:**
- `store_deinit` xóa `s_es`.
- Test mới `test_stream_cut_reboot` chạy trong pha `STORE_CUTS`. Nó stream một body trên 8 KB (nhiều record), cắt điện ở thao tác flash thứ 1, 4, 7, … 37, rồi reboot. Sau mỗi lần như vậy, lần lưu tiếp theo phải thành công, không bị BUSY.
- Việc giữ nguyên một thế hệ khi bị cắt điện vẫn do ma trận cắt điện sẵn có (`test_power_cuts`) kiểm.
- Trước khi sửa, test này đỏ: cả 7/7 lần cắt đều kết thúc bằng BUSY.

**3. Bench có thể PASS khi tải thử thất bại:**
- `mem_check.py`:
  - kiểm mọi lần POST/GET: 200, và GET trả đúng các byte đã lưu;
  - mỗi lần relay khởi động lại, MQTT phải kết nối lại (`wait_connects`);
  - mỗi 10 vòng, UART2 gửi một request HTTPS ngay trong lúc tải; đầu cuối HTTPS phải nhận đủ số request.
- `web_check heap_loop` kiểm mã trả về của từng endpoint trong mọi vòng.
- Hai thay đổi bench:
  - upload chứng chỉ chỉ chạy mỗi 10 vòng, vì board tự đóng kết nối upload nên mỗi lần để lại một TIME_WAIT;
  - một connect bị timeout được thử lại một lần sau 2 giây và được đếm. Trên bench WSL NAT, khoảng một trong vài nghìn connect bị timeout trong khi board vẫn trả lời request kế tiếp. Board treo thật thì cả hai lần đều hỏng.

**Kết quả:**
- Host: `test_store` (cả hai pha và `STORE_CUTS`) và `test_store_small` xanh.
- App build được, `check_elf` đạt.
- Board:
  - `mem_check` đạt: (e) heap free 185 384, thấp nhất 142 240, khối lớn nhất 170 880; heap giữ mức trong khoảng 1,6 KB; ADC 10,00 cửa sổ/s; watchdog cách tối đa 200 ms; 4/4 lần MQTT kết nối lại; HTTPS 138 request;
  - `heap_loop`: đạt 200 vòng, mọi endpoint trả đúng mã, heap 356 168 → 356 168 B, 3 connect phải thử lại (đều được trả lời ở lần thử lại).

**Phạm vi:** `git diff 37427c0 HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi). Nhớ là người dùng muốn giữ đơn giản.
