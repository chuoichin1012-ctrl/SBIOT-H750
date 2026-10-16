Plan 09c (RAM) đã làm xong trên branch `plan09c-ram`, từ 6c6e428b1f012c5e9f7ae11a40abf17e10526a9f tới 37427c0. Đây là bước review code bắt buộc trước PR.

Plan: `stm32/docs/plans/2026-10-16-09c-ram.md`. Các mục "Measurements" và "Bench findings" ghi số đo và những gì tìm được trên bench.

**Các commit:**
- T1 (fbbbd02): bỏ arena, cJSON cấp phát trên heap. `test_text_small` và `test_edge_text` chuyển sang `heap_meter.h`.
- T2 (599b55d): scanner `js_stream_*`.
- T3 (c4061bd): lưu edgeDevices theo luồng (begin/write/commit/abort; slot web nào begin thì sở hữu phiên). Kèm một sửa lỗi: `kv_read` không còn cấp phát theo một độ dài quay vòng.
- T4 (1b6e623): buffer tạm trên heap lúc boot; buffer heap cho chứng chỉ, được zeroize nếu là khóa; heap 352 KB.
- T5: `heapLargest`, `mem_check.py`, số đo, spec.

**Kết quả kiểm thử:**
- Host test xanh: `test_jsonscan`, `test_store` (cả hai pha và `STORE_CUTS`), `test_store_small`, `test_edge_core`, `test_edge_text`, `test_text_small`, `test_oom`, `test_web_core`, `test_cfgmodel`.
- Board: `mem_check` đạt. Heap thấp nhất là 139 592 B sau 50 vòng có TLS; heap ổn định; IO và watchdog giữ nhịp.
- Hồi quy: `bridge`, `edge`, `mqtt`, `tls` và `test:pages` đạt. Ca pin của `web_check` đã sửa lại phía bench.

**Phạm vi:** `git diff 6c6e428b1f012c5e9f7ae11a40abf17e10526a9f HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi), và nhớ là người dùng muốn giữ đơn giản.
