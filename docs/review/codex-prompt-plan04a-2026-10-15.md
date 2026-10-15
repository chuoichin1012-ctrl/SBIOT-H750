Plan 02 đã merge (PR #3 trên SBIOT-H750). Theo roadmap, tiếp theo là Plan 04. Tôi tách Plan 04 thành hai phần:
- **04a**, chạy trên host: định dạng image, bản ghi A/B, handoff block, toàn bộ logic cài đặt của bootloader (bảng quyết định hàng 1–10, đường RAM, cứu hộ có thử lại), bộ ghi staging của app, `mkimage.py`;
- **04b**, chạy trên board: driver SPI4, QSPI erase/program, tích hợp vào bootloader, upload qua UART7, deadline của TRIAL, loader QSPI tự viết.

Plan cần duyệt là `stm32/docs/plans/2026-10-15-04a-ota-host.md`, trên branch `plan04a-ota-host`.

Toàn bộ code trong plan đã chạy thử trước khi viết. Kết quả:
- `test_install` đạt;
- ma trận cắt điện: 1 099 điểm cho cài đặt và 1 416 điểm cho rollback, cắt tại **mọi** thao tác xóa/ghi, kể cả các lần ghi của app (upload, PENDING, confirm). Mọi điểm đều kết thúc ở một image hợp lệ đã xác nhận;
- đủ 10 hàng của bảng quyết định;
- `test_ota` (vector NIST, header, vector kiểm tra, image do `mkimage.py` tạo) và `test_mkimage` đạt;
- 16/16 mutant bị bắt.

Các quyết định D1–D9 nằm trong mục "Design decisions to review". Hãy xét kỹ những điểm sau:
- D2: lỗi đọc lại sau khi ghi checkpoint dẫn tới REBOOT, và lần thử đó đã được đếm;
- D4: cứu hộ có thử lại từ staging dùng WRITE(0), tức đi qua VERIFY rồi TRIAL(0);
- D6: ý nghĩa của trường `degraded` trong bản ghi;
- D8: `ota_stage_begin` từ chối mọi trạng thái khác CONFIRMED.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt:
- Plan 04a có chỗ nào lệch spec §11.2/§11.3/§12.1 không? Có ca kiểm thử nào trong danh sách §12.1 (dòng Bootloader) còn thiếu không? Có lỗi logic nào trong `install.c` không?
- Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`, dòng thứ hai là `THỰC THI: <SUBAGENT | NATIVE>`.
