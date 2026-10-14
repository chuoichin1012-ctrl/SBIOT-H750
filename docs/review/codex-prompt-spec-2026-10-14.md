Bạn là chuyên gia đánh giá độc lập về firmware nhúng cho gateway IoT công nghiệp. Hãy review **bản thiết kế (spec)** sau và CHỈ đọc, không sửa, tạo hay xóa bất kỳ tệp nào:

`stm32/docs/specs/2026-10-14-stm32-firmware-design.md`

Bối cảnh:
- Spec mô tả firmware mới cho board STM32H750VBT6. Sơ đồ nguyên lý của board là `docs/Job2.PDF` (14 trang, có thể trích văn bản bằng `pdftotext` nếu có).
- Firmware sẽ port từ firmware ESP32 hiện có trong `firmware/` (ESP-IDF 5.5, kiến trúc mô tả ở `firmware/docs/ARCHITECTURE.md`). Web UI và hợp đồng API nằm trong `src/`.
- Chức năng tham chiếu: `docs/USR-M100_Chuc_nang_chi_tiet.md`.
- Các quyết định ở §2 của spec (D1–D12) đã được chủ dự án chốt. Chỉ phản biện một quyết định khi nó gây lỗi kỹ thuật cụ thể, và nói rõ lỗi đó.

Hãy đánh giá theo các mục sau và trả lời bằng tiếng Việt:

1. **Đúng với phần cứng:** đối chiếu §3, §5 và §6.2 với schematic và với datasheet hoặc reference manual của STM32H750. Kiểm tra cụ thể:
   - bản đồ bộ nhớ (ITCM, DTCM, AXI, SRAM1–4) và khả năng truy cập của ETH, DMA1/2, BDMA;
   - sector flash nội;
   - QUADSPI bank 2 trên PB2/PC11/PE7–PE10;
   - DE phần cứng trên PA12/PD4, Receiver Timeout của USART;
   - kênh ADC của PC2_C/PC3_C;
   - 74HC573, mức logic của DI;
   - SGM706, GD25Q32E.
2. **Bootloader và OTA (§11):** tính đúng của cơ chế cài đặt làm tiếp được sau khi mất điện, boot thử và rollback, kiểm SHA-256, các trường hợp biên (mất điện khi ghi bản ghi trạng thái, image backup hỏng, OTA trong lúc TRIAL), watchdog trong bootloader, phân chia khởi tạo giữa bootloader và app khi chạy XIP.
3. **Bộ nhớ và DMA (§5):** ngân sách RAM theo từng vùng có khả thi không. Quy tắc DMA và cache có đủ và đúng không. Định tuyến bộ cấp phát (newlib, mbedTLS, Mongoose, cJSON theo chủ sở hữu) có lỗ hổng gì. Arena 64 KB và các giới hạn body có hợp lý không.
4. **Đồng thời và runtime (§6):** ưu tiên task, quy tắc luồng của Mongoose và coreMQTT, khả năng deadlock hoặc đảo ưu tiên, việc chia sẻ hai cổng UART giữa bridge, MQTT, Edge và console, thứ tự khởi động.
5. **Edge xử lý theo node (§9):** pipeline có tương đương với `cfg_validate` nguyên khối trong `firmware/components/cfgmodel/cfgmodel.c` không. Có quy tắc kiểm tra liên thiết bị nào bị bỏ sót không. Việc đổi generation trong FlashDB có an toàn khi mất điện không.
6. **Mạng (§7, §8):** PPPoS qua CMUX trên EC600U khi không có RTS/CTS, ưu tiên ETH/LTE, cấu hình lwIP (khoảng 64 netconn, `MEM_SIZE`), TLS khi chưa có giờ, coreMQTT với outbox tự viết và bản vá MQTT 3.1.
7. **Tiêu chuẩn:**
   - Modbus RTU (t1.5/t3.5, CRC), Modbus TCP/MBAP;
   - MQTT 3.1/3.1.1 (QoS 1/2, gửi lại, clean session);
   - 3GPP 27.010 (CMUX);
   - TLS 1.2;
   - an ninh nhúng (IEC 62443, ETSI EN 303 645) ở mức ghi chú: F01 đã được chấp nhận, chỉ nêu khi spec làm tình hình xấu hơn bản ESP32.
8. **Kiểm thử (§12):** độ phủ, các rủi ro chưa có test, tính khả thi của bench HIL, những gì cần đo trên phần cứng.
9. **Tính nhất quán của spec:** mâu thuẫn giữa các mục, tham chiếu chéo sai, yêu cầu mơ hồ (có thể hiểu theo hai cách), hành vi của bản ESP32 bị mất mà §13 không ghi nhận.
10. **Danh sách phát hiện**, xếp theo mức Nghiêm trọng / Cao / Trung bình / Thấp. Mỗi phát hiện ghi:
    - mục của spec (§) và, nếu có, tệp:dòng trong mã ESP32;
    - mô tả;
    - rủi ro;
    - đề xuất sửa spec.
11. **Kết luận:** spec đã đủ để lập kế hoạch triển khai chưa. Những gì phải sửa trước.

Chỉ nêu những gì bạn kiểm chứng được trong spec, mã nguồn, schematic hoặc tài liệu chính thức (ghi nguồn). Ghi rõ khi một nhận định là suy luận.
