**NOT APPROVED**

Còn hai điểm cần sửa:

1. **Timeout vẫn mâu thuẫn với §5.6** — [plan:61](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-16-08a-bridge.md:61), dòng 96. `NET_AGAIN` gộp cả thiếu bộ nhớ; vì vậy thiếu bộ nhớ kéo dài 10 s vẫn làm đóng TCP hoặc bỏ datagram. Không có căn cứ cho câu “Memory shortages last milliseconds”, cũng không thể suy ra peer hỏng chỉ từ việc `send` không tiến triển. Nếu chọn giới hạn 10 s để giải phóng tài nguyên, cần ghi rõ đây là **ngoại lệ có chủ đích trong §5.6**, sửa câu “temporary error never closes”, và test thiếu bộ nhớ kéo dài qua ngưỡng.

2. **`select(..., 10 ms)` không bảo đảm nhường CPU** — [plan:95](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-16-08a-bridge.md:95). Khi socket liên tục sẵn sàng, `select` trả ngay; bridge ưu tiên 8 có thể làm đói `io` ưu tiên 6. Cần giới hạn công việc mỗi lượt và thực sự block, chẳng hạn `vTaskDelay(1)` khi hết ngân sách; thêm ca tải liên tục kiểm task `io` vẫn chạy.

Các sửa về offset gửi, tên API lwIP, gate `accept` và ngân sách stack đã phù hợp.