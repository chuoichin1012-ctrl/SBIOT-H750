Plan 09d lượt 2, sau `codex-gpt-6-astra-plan09d-2026-10-16.md`.

Đã sửa T3 của `stm32/docs/plans/2026-10-16-09d-300-points.md`:
- `mem_check.py` dùng model đủ 300 điểm.
- Template chỉ đặt tên khoảng 100 điểm, trong giới hạn 26 624 byte.
- Bench kiểm rằng Edge chạy và report tới được broker MQTT.
- Body ở (d) và (e) là 96 000 byte.

Ở "Known limits", tôi sửa lại: 4 sector còn lại chứa manifest, các key nhỏ và chỗ cho GC.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa.
