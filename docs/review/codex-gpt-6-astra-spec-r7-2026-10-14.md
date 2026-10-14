QUYẾT ĐỊNH: APPROVED

Kế hoạch triển khai bắt buộc bao gồm:

- **P1 — Bộ nhớ:** bảng pool/buffer/mailbox lwIP theo vùng; linker-map và đo heap/stack trong hai kịch bản §12.3.
- **P2 — Runtime/ADC:** giới hạn công việc, block/backpressure, test starvation/flood; bàn giao buffer ADC và nghiệm thu cả cấu hình dự phòng nếu dùng.
- **P3 — Sản xuất/handoff:** factory record, loader SPI4, bảo toàn hiệu chuẩn; CRC, vòng đời và reset các bộ đếm handoff.
- **OTA:** fault injection cho mọi hàng quyết định, đường RAM, timeout, SHA sai, mất nguồn và trường hợp chỉ QSPI còn hợp lệ.
- **Giao thức/HIL:** modem ON/OFF/UNKNOWN và xung PWRKEY 2,5 s; CMUX flow control, PPP cụt giữa callback; MQTT reconnect/QoS và JSON round-trip/413.

Đã kiểm commit `8a52dbd`: điều kiện cuối cùng được xử lý đúng tại [§11.2](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:878), kèm [ca kiểm thử §12.1](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:984). Phê duyệt để lập kế hoạch; chưa xác nhận firmware/HIL đạt.