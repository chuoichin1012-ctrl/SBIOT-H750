Plan 09b r2: `stm32/docs/plans/2026-10-16-09b-config.md` đã sửa đủ 5 điểm của r1.

1. Bản lưu tách khỏi bản runtime (ruling 4):
   - cache giữ byte thô cho GET, kể cả với config `INVALID`;
   - `store_get` chỉ dùng được trong lúc boot (nó parse rồi chạy `cfg_load`); sau `store_boot_done()` nó trả NULL;
   - trạng thái là của lần boot hiện tại; lưu config mới không làm đổi runtime hay trạng thái;
   - có test cho cả ba ý trên (T3).
2. Safe mode (ruling 5/6) áp dụng khi `network`, `system` hoặc `io` sai, hoặc DB cfg không đọc được:
   - DO bị từ chối ở mọi đường: `/api/io` trả 409, Modbus IO control trả exception 4, linkage không có tác dụng;
   - `serial_*` trả −1 trên port chưa khởi động;
   - test trên board: port2 sai trong khi MQTT và Edge có bind vào nó; `io` sai rồi thử bật DO theo cả 3 đường; Reload qua `R 5000`.
3. Ruling 10 / ruling 2:
   - `store_init` ghi trạng thái cho từng DB, không thoát sớm;
   - reset chỉ đụng các DB đã mount. Khi FAL đang khóa thì không ghi, không xóa gì, báo `incomplete` rồi khởi động lại; khóa FAL được giữ nguyên;
   - có test cho trường hợp mount lỗi và trường hợp FAL khóa.
4. Template: giữ đúng spec §9.4, tức chỉ tắt report, Edge vẫn chạy; `edgeReport` không qua được bộ nạp thì Edge tắt. UI port lại `edge_tmpl_size` với giới hạn 26 624 B, fixture có kèm mô hình.
5. Hai gate dữ liệu:
   - T1 cắt điện ở mọi lần ghi **và** xóa, kể cả xóa sector dở dang;
   - Review Focus 1 dùng ảnh flash do writer ở revision `850bdc6` sinh ra (`tools/pre09b_image.sh`, dựng trong worktree tạm), kiểm GET giống từng byte và runtime tương đương. Mô tả record cũ đã sửa lại: cắt theo byte ở 8 KB.

Trả lời APPROVED hoặc NOT APPROVED kèm danh sách cần sửa.
