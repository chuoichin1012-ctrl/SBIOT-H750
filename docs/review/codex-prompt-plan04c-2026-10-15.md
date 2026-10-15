Tiếp tục trong cùng phiên. Plan 04a đã merge (PR #4). Người dùng muốn thêm **chống clone**: firmware chép sang vi điều khiển khác thì không chạy, và nếu sai thì xóa QSPI rồi dừng. Theo quyết định mới nhất của người dùng, "chuyển sang ghi tag lúc nạp code, bootloader chỉ kiểm, không bao giờ tự tạo tag".

Branch `plan04c-anticlone`, xem `git diff be53627..a1182af -- stm32/`. Commit f9ce1cc là bản HMAC tự ghi tag, đã bị a1182af thay thế. Tài liệu:
- spec §11.5, §5.1, §12.1 (hàng "Chống clone"), §12.4 (bước sản xuất);
- roadmap hàng 04c;
- plan `stm32/docs/plans/2026-10-15-04c-anticlone-host.md`, có 5 ruling.

**Thiết kế:**
- **Bản ghi ở QSPI 0x3F0000**, 84 B: magic `SBBD`, UID (chỉ để người đọc), chữ ký Ed25519 trên `"SBIOT-H750 bind"` ‖ UID, và CRC32.
- **Ghi lúc nạp:**
  - `tools/provision.py` ký bằng Python `cryptography`;
  - `tools/flash.sh bind KEY|dev` đọc UID bằng `-r32 0x1FF1E800 12`, rồi ghi bản ghi qua loader của nhóm. Đã chạy được trên board thật.
- **`bind.c` dùng Monocypher 4.0.2**, được vendor kèm ghi chú nguồn gốc.
  - Thiếu bản ghi, magic dở, CRC sai hoặc lỗi đọc: dừng, không ghi và không xóa gì.
  - CRC đúng mà chữ ký sai **cho UID của chip này**: xóa slot ứng dụng (0x000000–0x16FFFF), giữ bản ghi, rồi dừng.
- **Khóa:** CMake nhận khóa công khai qua `SBIOT_BIND_PUBKEY_FILE`, không có thì dùng khóa dev (sinh từ một chuỗi công khai) và cảnh báo.
- **RDP Level 1** là bước sản xuất cuối cùng.
- **Ed25519 thay vì HMAC** (ruling 1). Nếu dùng HMAC thì app phải mang khóa trong QSPI, ai đọc được QSPI cũng tự tạo được bản ghi.

**Kết quả:**
- `test_bind` kiểm các bản ghi do Python ký bằng Monocypher;
- `test_provision` đạt;
- mutants 24/24;
- toàn bộ host suite đạt, boot và app vẫn build.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt:
- Có lỗi bảo mật hay đúng/sai nào không? Có kịch bản nào làm board thật bị xóa nhầm, hoặc board clone vẫn chạy được (ngoài các giới hạn đã nêu thẳng trong spec) không?
- 5 ruling có chấp nhận được không?
- Có thiếu ca test quan trọng nào không?
- Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
