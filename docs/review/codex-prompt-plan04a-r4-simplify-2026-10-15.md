Tiếp tục trong cùng phiên. Người dùng đã **bỏ toàn bộ cơ chế tự cứu hộ** của bootloader (Plan 04a). Nguyên văn: "Bootloader không cần phải tự cứu hộ, nó chỉ nên có chức năng kiểm tra có bản fw mới và ghi vào qspi, nếu không thành công người dùng cần tác động để khôi phục. Hãy đơn giản hoá công việc."

Tôi đã làm lại theo hướng đó, commit `af7d8e0` trên branch `plan04a-ota-host`.

**Đã thay:**
- spec §5.2, §11.2, §11.3, §11.4, §12.1, D9 và roadmap 04a/04b;
- `boot/install.{h,c}` và `components/ota/ota_app.{h,c}` (đã bỏ `bootrec` và `handoff`);
- `test/host/test_install.c`, `test/host/mutants_ota.py`;
- plan doc `stm32/docs/plans/2026-10-15-04a-ota-host.md`, có mục "Rulings" gồm 5 quyết định tôi tự đưa ra.

**Thiết kế mới:** không bản ghi trạng thái, không TRIAL/rollback, không backup. Có ba image: `Q` (QSPI), `S` (staging) và `F` (slot factory ở SPI4 0x280000). Trình tự mỗi lần boot:
1. Reload giữ 5 s → khôi phục firmware gốc:
   - kiểm F, hỏng thì dừng và không ghi gì;
   - xóa sector đầu của staging;
   - `copy` F → QSPI.
2. Q hợp lệ và slot factory chưa có magic → `copy` Q → factory (gieo một lần sau sản xuất). Thất bại thì chỉ báo.
3. S hợp lệ, và Q không hợp lệ hoặc header Q ≠ S → `copy` S → QSPI. Thất bại thì dừng.
4. Q hợp lệ thì nhảy, ngược lại thì dừng (chờ Reload hoặc SWD).

`copy` chạy theo thứ tự:
1. xóa sector đầu của đích trước;
2. xóa và ghi từng sector còn lại;
3. ghi sector đầu;
4. ghi riêng 4 byte magic `SBIO`, là lần ghi cuối cùng;
5. `img_check` đích, header phải trùng với nguồn.

**Kết quả:**
- `test_install` đạt: ma trận cắt điện ở mọi thao tác (install 759, restore 298, seed 297 điểm), mỗi ma trận thêm 300 lần chạy với tối đa 3 lần cắt;
- mutants 14/14;
- toàn bộ host suite đạt, boot và app vẫn build.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt:
- Review code (`git diff ad7ae86..af7d8e0 -- stm32/`) và spec: có lỗi, kịch bản hỏng nào, hay chỗ spec mâu thuẫn hoặc còn sót tham chiếu tới thiết kế cũ (TRIAL, backup, bản ghi trạng thái, handoff block) không?
- 5 ruling trong plan doc có chấp nhận được không?
- Có ca test quan trọng nào còn thiếu không?
- Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
