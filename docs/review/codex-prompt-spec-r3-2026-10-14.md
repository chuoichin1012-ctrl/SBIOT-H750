Vòng 3, tiếp tục trong cùng phiên. Spec đã được sửa theo quyết định NOT APPROVED của bạn ở vòng 2. Commit: `746186b`. Xem `git diff 48a8e63 746186b -- stm32/docs/specs/`.

Bảng xử lý từng mục R2-01…R2-10 nằm ở phần "Vòng 2" của `stm32/docs/review/codex-spec-2026-10-14-followup.md`. Tóm tắt:
- **R2-01:** §11.2.
- **R2-02:** §8.
- **R2-03:** §6.6 viết lại.
- **R2-04:** §7.2.
- **R2-05:** §9.3 và §10.
- **R2-06:** §5.3 và §9.4, giới hạn buffer report 26 KB.
- **R2-07:** §6.1. `serial_rx` = 26, `cmux_rx` = 25, đều trên `tcpip` = 24.
- **R2-08:** §7.2, bảng timing của EC600U.
- **R2-09:** bảng nghiệm thu ADC ở §6.6.
- **R2-10:** các chỗ không thống nhất.

Ghi chú:
- Câu "ADC3 chỉ có BDMA" đã được cải chính thành "lựa chọn thiết kế".
- Mục 16 (bảng pool lwIP) vẫn để cho kế hoạch, như đã nêu trong follow-up.

Yêu cầu, vẫn CHỈ đọc, trả lời bằng tiếng Việt:

1. Với từng mục R2-01…R2-10, và các mục #2, #4, #11, #12, #15, #16, #17, #19 của vòng 1: **Đạt / Chưa đạt**. Nếu chưa đạt, nêu lý do, mục của spec, và nguồn.
2. Lỗi mới phát sinh do lần sửa này: mâu thuẫn, con số, tham chiếu chéo. Chú ý thêm:
   - việc nâng ưu tiên `serial_rx` và `cmux_rx` lên trên `tcpip` có gây vấn đề gì không;
   - factory record ở SPI4 0x3F0000;
   - giới hạn 10 KB cho các cấu hình nhỏ.
3. Quyết định. Dòng đầu tiên phải là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
   - Với `APPROVED WITH CONDITIONS`, các điều kiện phải cụ thể, kiểm được, và ghi rõ điều kiện nào phải sửa trong spec, điều kiện nào được phép giải quyết trong kế hoạch triển khai.
   - Chỉ chặn vì lỗi kỹ thuật có căn cứ, không chặn vì những chi tiết vốn thuộc về kế hoạch.
