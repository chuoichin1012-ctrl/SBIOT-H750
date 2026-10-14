QUYẾT ĐỊNH: NOT APPROVED
THỰC THI: NATIVE

Có **một lỗi kỹ thuật chặn** và vài điểm cần sửa trong plan:

1. **Thiếu workaround QUADSPI sau reset** — [Task 4, `qf_init()`](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-02-board-bringup.md:1364). Errata hiện hành **ES0392 Rev 15 §2.7.4**, áp dụng cả Rev V, yêu cầu chuỗi xử lý *internal timing criticality* sau reset và khi rời memory-mapped; bước disable phải hoàn tất trong **127 kernel clocks**. Plan và HAL v1.11.6 đã khóa đều chưa có chuỗi này. Bổ sung vào đường khởi tạo bootloader, kiểm disassembly/timing và chạy lại bench. FSIZE=22 chỉ xử lý lỗi khác, tại §2.7.5. [Nguồn ST](https://www.st.com/resource/en/errata_sheet/es0392-stm32h742xig-stm32h743xig-stm32h750xb-stm32h753xi-device-errata-stmicroelectronics.pdf).

2. **Review Focus vượt phạm vi test thực tế** — [dòng 97–98](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-02-board-bringup.md:97). Mười software reset và ba IWDG reset chưa tạo tình huống flash đang ghi status hoặc đang continuous-read. Cần ca bench chủ động tạo các trạng thái đó. Ngoài ra, kiểm hai vector **không thể bảo đảm từ chối image ghi dở nếu vector vẫn hợp lệ**; sửa lời hứa này và giao kiểm toàn vẹn cho Plan 04 như phần ngoại lệ đã nêu.

3. **Lệnh debug sai đường dẫn** — [Task 6, Step 3](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-02-board-bringup.md:2798). `$PWD` được mở rộng sau `cd /mnt/c`, nên trỏ tới `/mnt/c/stm32/...`. Tính đường dẫn config trước khi đổi thư mục.

4. **BOR diễn giải sai** — [Task 6, Step 5](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-02-board-bringup.md:2820). Giữ `BOR_LEV=2` đúng spec, nhưng sửa “reset dưới khoảng 2,7 V”: ngưỡng giảm điển hình là **2,31 V**, ngưỡng tăng **2,41 V**. [Datasheet, bảng đặc tính reset Rev V](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf#page=202).

5. **Script Task 1 khớp nhưng chưa đồng bộ hết:** tôi kiểm trong bộ nhớ, đủ **14 + 2** thay thế; §14 vẫn yêu cầu `system_app.c` riêng. Sửa dòng đó theo startup mới. Các bước kiểm phiên bản/build/mutant cũng cần dừng khi lệnh thất bại, tránh kiểm ELF cũ sau build lỗi.

**Sáu quyết định:** chấp nhận **1–4** về thiết kế; **5** chấp nhận FSIZE, tốc độ GPIO và alternate phase, nhưng phải bổ sung workaround trên; **6** chấp nhận giới hạn trên board R20 chưa lắp và loader đúng SHA.

Chọn **Native** vì các task phụ thuộc chặt vào cùng board và trạng thái nạp/debug. Tôi chưa chạy lại bench; các số đo B1–B12 vẫn là bằng chứng do bạn cung cấp.