Plan 08c đã làm xong trên branch `plan08c-edge` (từ 4c7fbeb365ade12bc35111b3728eb278a857cb73 tới 02ceef7). Xin review code bắt buộc trước PR.

Plan: `stm32/docs/plans/2026-10-16-08c-edge.md` (đã duyệt r3). Kết quả bench và số đo nằm trong các mục "Bench findings" và "Measurements". Ruling khi thực hiện nằm trong ledger `.superpowers/sdd/2026-10-16-08c-edge/progress.md`.

**Các commit:**
- T1 `cfg_edge_load`: 08c7ab8;
- T2 `edge_core`: 260b333;
- T3 bridge (slot report, hook Edge, `frame_push`, xả request dang dở): 3663874;
- T4 `edge_rt`, services, web: 61cbbf8;
- T5 bench, cùng các sửa lỗi tìm ra trên bench:
  - newlib-nano không in số thực (`-u _printf_float`);
  - `EDGE_MAX_TASKS` phải là 9;
  - log bước khởi động thất bại;
  - script upload trả về sớm.

**Kết quả kiểm thử:**
- Host test xanh: `test_edge_core`, `test_cfgmodel`, `test_edge_text` (8090 mẫu giống hệt từng byte), `test_text_small`, `test_oom`, `test_bridge_core`, `test_tlsconn`, `test_clock`.
- Bench: `edge_check.py` qua đủ 70 kiểm tra, cộng ca `di` (DO1 nối DI1).
- Hồi quy: `bridge_check.py` 55/55; `tls_check.py tcpclient https` đạt.

**Phạm vi review:** `git diff 4c7fbeb365ade12bc35111b3728eb278a857cb73 HEAD -- stm32`.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi Critical/Important (file:dòng, kịch bản lỗi). Nhớ là người dùng muốn giữ đơn giản.
