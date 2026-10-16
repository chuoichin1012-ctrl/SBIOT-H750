Plan 10c đã làm xong trên branch `plan10c-review-fixes`, từ 8bd5b88 (plan được duyệt ở r2) tới HEAD. Đây là bước review code bắt buộc trước PR. Plan: `stm32/docs/plans/2026-10-17-10c-review-fixes.md`. Ledger và các ruling: `.superpowers/sdd/2026-10-17-10c-review-fixes/progress.md`.

**Các task (lỗi bạn tìm ra khi review toàn bộ dự án):**
1. `CJSON_NESTING_LIMIT=16` cho firmware (CMake) và test host (Makefile); UI giới hạn template 16 tầng. Test: `test_depth_limit`.
2. Cache store giữ tên tĩnh của cfgmodel (`canonical`). Test: `test_cache_name_owned`, chạy với cache nhỏ.
3. `bridge_mode_name` chặn chỉ số. Đã rà các mảng khác được đánh chỉ số bằng giá trị config: không có chỗ nào khác không bị chặn. Test: `test_mode_name`.
4. Vòng chờ khung serial dùng chung deadline của transaction. Đây là code chạy trên board; ruling đã ghi.
5. `mb_map_t` có `begin`/`end` quanh vòng đọc FC3/FC4; Edge khóa cache một lần cho cả vòng. Test: `test_serve_snapshot`.
6. `ota_stage_claim`/`ota_stage_release` với CAS. Danh tính chủ là phiên của bên gọi (slot web, lượt UART7), không phải `ota_stage_t`, vì web dùng chung một `s_ota` (ruling). Test: `test_stage_owner`.
7. `rw_prot` không rò bộ nhớ khi OOM. Test: `test_rwprot_oom` (LSan).
8. NO SIM trên UART thường không RESET_N, chờ 60 s rồi hỏi lại. Code chạy trên board; ruling đã ghi.
9. `web_send_poll` dùng một đồng hồ gửi; hết `WEB_STALL_MS` mà không ghi được byte nào thì đóng kết nối. Test: `test_send_stall`.
10. Placeholder phải parse được toàn bộ chuỗi (`cJSON_ParseWithOpts(..., true)`). Test: `tmpl` với `1abc`, `null-device`, `truex`.
11. Ba lỗi Minor:
    - cổng WebSocket trùng cổng web khi console bật;
    - `/api/status` báo `enabled` theo trạng thái lúc boot (`mqttgw_enabled`, `edge_enabled`);
    - `reportModeError` cho phép chỉ báo theo thay đổi.

**Kết quả:**
- **Host:** `test_proto`, `test_store`, `test_store_small`, `test_bridge_core`, `test_install`, `test_web_core`, `test_edge_core`, `test_edge_text` cùng các test khác dùng cJSON đều xanh; web unit 16/16; tsc đạt.
- **Board:**
  - `review_check`: body JSON 300 tầng trả 400 và gateway vẫn chạy; lưu MQTT bật mà chưa restart thì Status vẫn báo tắt; upload thứ hai ngay sau một upload thành công trả 409, sau đó image thứ nhất được cài;
  - `defaults_check` và `web_check plain configs` đạt;
  - `pages.mjs` đạt, nhưng có 2 lần timeout ở bước Network/APN, cả hai ngay sau factory reset, và đã từng xảy ra trước Plan 10c (ruling).

**Phạm vi:** `git diff 8bd5b88 HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi). Người dùng muốn giữ đơn giản.
