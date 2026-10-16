Plan 09 (MQTT) đã làm xong trên branch `plan09-mqtt` (từ 3ee0c0cea61aad9ed5d294030f44e0a76830c8bf tới 36ef8a2). Xin review code bắt buộc trước PR.

Plan: `stm32/docs/plans/2026-10-16-09-mqtt.md` (đã duyệt r2). Kết quả bench và số đo nằm trong các mục "Bench findings" và "Measurements". Ruling khi thực hiện nằm trong ledger `.superpowers/sdd/2026-10-16-09-mqtt/progress.md`.

**Các commit:**
- T1 coreMQTT 2.3.1 nguyên bản (e5a7971); bản vá 3.1 và `core_mqtt_config.h` (5e0be1d);
- T2 `mqtt_transport` (7db46dd);
- T3 `mqtt_outbox`, `mqtt_sess`, bản vá ack cho tin quá 4 KB (8432bb0);
- T4 `mqttgw` (task `mqtt` và `mqtt_io`, ring SRAM4, outbox SRAM2+3), Edge kênh 0, `mqtt.state`, các gate `check_elf`, spec §8 (dbdb323);
- T5 bench, cùng hai lỗi tìm ra trên bench (36ef8a2):
  - lần chờ `retryInterval` bị notify cắt ngắn (mỗi tin xếp hàng khi `bufferOffline` lại dial ngay), nay là `vTaskDelay`;
  - `/api/status` `stackMin` rỗng khi có khoảng 30 task (mảng 24), nay là mảng tĩnh 40 trong AXI.

**Kết quả kiểm thử:**
- Host test xanh: `test_mqtt_wire`, `test_mqtt_transport`, `test_mqtt_outbox`, `test_mqtt_session`, `test_edge_core`.
- Bench: `mqtt_check.py` đủ 11 case, 112 kiểm tra (gồm ma trận QoS 12 pha: không mất tin, phiên giữ không trùng QoS 2); `edge_check.py mqtt memory` đạt.
- Hồi quy: `edge_check.py basic remote`, `tls_check.py tcpclient`, `bridge_check.py tcpserver` đạt.

**Phạm vi review:** `git diff 3ee0c0cea61aad9ed5d294030f44e0a76830c8bf HEAD -- stm32` (bỏ qua `third_party/coremqtt/source` nguyên bản; bản vá là `third_party/patches/coremqtt-2.3.1-sbiot.patch`).

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi Critical/Important (file:dòng, kịch bản lỗi). Nhớ là người dùng muốn giữ đơn giản.
