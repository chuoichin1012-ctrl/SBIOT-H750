Review cuối cho Plan 04a (`stm32/docs/plans/2026-10-15-04a-ota-host.md`), branch `plan04a-ota-host`, phạm vi `git diff main..HEAD`. Mã cần xét:
- `stm32/firmware/components/ota/`;
- `stm32/firmware/boot/install.{c,h}`;
- `stm32/firmware/test/host/{nor_sim.*,test_ota.c,test_install.c,mutants_ota.py,Makefile}`;
- `stm32/firmware/tools/{mkimage.py,test_mkimage.py}`;
- roadmap và `.gitignore`.

Đã chạy trên revision cuối:
- **Host:** `make -C stm32/firmware/test/host` đạt cả 10 bộ (test_ota, test_mkimage, test_install cùng 7 bộ cũ). Ma trận cắt điện cho cài đặt có 1 099 điểm, cho rollback 1 416 điểm, cộng 2 × 300 lượt cắt 3 lần, đều đạt. Cả 10 hàng của bảng quyết định đều được chạy tới.
- **Mutant:** `mutants_ota.py` bắt được 20/20.
- **Gate khác:** `check_pins` đạt, cây ESP32 không đổi.

Review Focus của plan:
1. Cắt điện ở mọi thao tác, kể cả các lần ghi của app.
2. Không bao giờ nhảy vào image không qua được bước kiểm.
3. SPI4 lỗi ở vùng bản ghi (hàng 3–6, lỗi đọc và ghi, lỗi chỉ xảy ra một lần).
4. Lỗi không báo gì (ghi im lặng, dữ liệu bị đổi sau khi đã so sánh).
5. Test đạt mà không thực sự kiểm gì.

Ledger:
Spec: stm32/docs/specs/2026-10-14-stm32-firmware-design.md. Approval: Codex r1 NOT APPROVED, r2 APPROVED WITH CONDITIONS (both done in r3, 0009a35). Execution: Native. Branch plan04a-ota-host from main c041f9f.
Task 3: Ruling: stm32/firmware/test/bench/__pycache__ (left by Plan 02 bench runs) added to stm32/.gitignore — the plan's 'only the new script shows up' needs it — cost if wrong: none.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt. Liệt kê lỗi theo mức Critical, Important, Minor, mỗi lỗi kèm file:dòng và kịch bản sai. Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
