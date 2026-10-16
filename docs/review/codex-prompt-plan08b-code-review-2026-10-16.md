Plan 08b đã làm xong trên branch `plan08b-tls` (từ 3588dad541a84e4d4cec9c828d7d71e091af0937 tới ab16669). Xin review code bắt buộc trước PR.

Plan: `stm32/docs/plans/2026-10-16-08b-tls.md` (đã duyệt r2). Kết quả bench và số đo nằm trong mục "Bench findings" và "Measurements". Các ruling khi thực hiện nằm trong ledger `.superpowers/sdd/2026-10-16-08b-tls/progress.md`, trong đó có:
- `tls_open` thêm tham số ra `verify_flags`;
- PKI test được sinh bằng `mkpki.py` (Python cryptography);
- gate ITCM chuyển sang T3;
- bench `twoports` giãn nhịp request vì va chạm trên vòng RS485 half-duplex.

**Các commit:**
- T0: c1dcb2e và 6289802;
- T1 (giờ hiển thị 2020): 802e2ea;
- T2 (vendor mbedTLS 3.6.7, `tls_config.h`, `tls_core`, `tls_port`): f825b90;
- T3 (`tlsconn`, `svc_net`, bridge, `wait_ready`, gate ITCM): 660c877;
- T4 (F20 trong `web.c`): e10e7d2;
- T5 (bench): ab16669.

**Kết quả kiểm thử:**
- Host test đều xanh: `test_tls_core`, `test_tlsconn` (10 ca qua loopback, có ASan), `test_bridge_core`, `test_clock`, `test_store` (kể cả `STORE_CUTS`), `test_store_small`, `test_cfgmodel`, `test_web_core`.
- Bench: `tls_check.py` 46/46 trên board vừa xoá; `bridge_check.py tcpclient http load twoports` 21/21; các ca `web_check` liên quan đều đạt.

**Phạm vi review:** `git diff 3588dad541a84e4d4cec9c828d7d71e091af0937 HEAD -- stm32`, trừ `third_party/mbedtls` (code gốc, không patch, SHA-256 ghi trong README của thư mục đó).

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi Critical/Important phải sửa (file:dòng, kịch bản lỗi). Nhớ là người dùng muốn giữ đơn giản.
