Vòng 4, tiếp tục trong cùng phiên. Ở vòng 3 bạn đã APPROVED WITH CONDITIONS. Các điều kiện S1–S7 đã được sửa trong commit `94db552`. Xem `git diff 746186b 94db552 -- stm32/docs/specs/`.

| Điều kiện | Chỗ sửa |
|---|---|
| S1 | §11.2: bảng hành động khi `attempts` > 3 theo từng pha (WRITE → RESTORE khi backup hợp lệ); cứu hộ chỉ trong hai trường hợp; hai loại deadline tách rời; ghi bản ghi trạng thái lỗi → handoff block trong backup SRAM, `rec_fail`, `degraded`. Gate ở §12.1 sửa theo |
| S2 | §8: điều kiện đạt khác nhau giữa phiên được giữ và phiên mới (phiên mới được phép trùng payload); tác dụng phụ retained message khi đăng ký lại |
| S3 | §6.6: dự phòng duy nhất là 387,5 cycle kèm ×8 (khoảng 634 µs); quy tắc scan < 900 µs; §14 sửa theo |
| S4 | §7.2: FCoff chừa DLC0; callback PPPoS trả số byte (0 khi hàng đợi đầy), lwIP chuyển thành `ERR_IF`, đếm `ppp_out_drop`; §12.1 sửa theo |
| S5 | §9.3, §9.4, §10.1: giới hạn ghi bằng byte (10.240 và 49.152); 10.240 là giới hạn chấp nhận, không phải cực đại của schema; test hai nhóm (round-trip, và 413 không làm đổi cấu hình đã lưu) |
| S6 | §7.2: tắt xong chỉ khi nhận `POWERED DOWN` (deadline 60 s); khởi động dùng RESET_N trước, PWRKEY sau; không bao giờ coi UART im lặng là đã tắt |
| S7 | §5.3: AXI tách phần tĩnh (khoảng 185 KB) và phần heap 256 KB, kèm mức dùng dự kiến bên trong heap; mỗi allocation chỉ tính một lần |

P1–P3 sẽ được giải quyết trong kế hoạch triển khai, như bạn cho phép.

Yêu cầu: CHỈ đọc, trả lời ngắn bằng tiếng Việt.
1. Với từng S1–S7: **Đạt / Chưa đạt**, kèm lý do nếu chưa đạt.
2. Có lỗi mới nào do lần sửa này gây ra không.
3. Dòng đầu tiên phải là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`. Nếu `APPROVED`, liệt kê lại P1–P3 (và điều kiện bổ sung nếu có) mà kế hoạch triển khai bắt buộc phải bao gồm.
