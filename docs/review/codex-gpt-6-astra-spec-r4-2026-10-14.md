QUYẾT ĐỊNH: APPROVED WITH CONDITIONS

Đã đối chiếu diff và spec tại `94db552`. Được tiếp tục lập kế hoạch, nhưng **S1, S6 và một câu của S7 chưa khép kín**. Không thay đổi tệp.

**1. Kiểm S1–S7**

| Điều kiện | Kết quả | Nhận xét |
|---|---|---|
| **S1** | **Chưa đạt** | §11.2 vẫn có các nhánh cứu hộ và deadline mâu thuẫn; chi tiết bên dưới. |
| **S2** | **Đạt** | Đã phân biệt bảo đảm giữa phiên được giữ và phiên mới; mô tả retained message khi resubscribe. |
| **S3** | **Đạt** | Dự phòng ×8 cho thời gian scan **633,6 µs**, dưới 900 µs. Phải nghiệm thu lại đầy đủ độ chính xác/nhiễu khi dùng phương án này. |
| **S4** | **Đạt** | DLC0 được miễn FCoff; callback trả số byte, không còn hứa trả `ERR_MEM`. Có một diễn giải về ranh giới PPP cần chỉnh bên dưới. |
| **S5** | **Đạt** | Giới hạn byte và hai nhóm test đã đúng: round-trip hoặc 413, giữ nguyên cấu hình. |
| **S6** | **Chưa đạt** | §7.2 vừa cấm suy ra trạng thái tắt từ UART im lặng, vừa yêu cầu làm đúng điều đó sau RESET_N. |
| **S7** | **Chưa đạt — nhẹ** | Các phép cộng đúng: tĩnh 185 KiB, AXI chưa cấp 71 KiB; heap dự kiến dùng 229 KiB, dư 27 KiB. Nhưng câu “Các buffer lớn đều cấp phát tĩnh” vẫn mâu thuẫn với report cấp từ heap. [§5.3](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:161). |

**2. Lỗi còn lại hoặc mới phát sinh**

- **OTA — cần sửa trong spec:** câu “cứu hộ chỉ trong hai trường hợp” không bao gồm nhánh mới **`rec_fail ≥ 3` khi WRITE/RESTORE**, nơi backup vẫn có thể hợp lệ và RESTORE chưa cạn lượt thử. Nhánh “QSPI không hợp lệ: cứu hộ” cũng chồng lên nhánh reset/thử lại khi QSPI đang ghi dở. Ngoài ra, đoạn watchdog cũ vẫn nói **mọi deadline đều ngừng kick**, trái với ngoại lệ ghi record vừa thêm. Cần hợp nhất thành các nhánh loại trừ nhau và cập nhật gate §12.1 theo đúng các trường hợp đó. [§11.2](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:859). Cạn lượt thử do mất nguồn liên tiếp cũng **không tự chứng minh lỗi phần cứng flash**.

- **Modem — cần sửa trong spec:** bước 3 vẫn ghi “không phản hồi sau deadline thì coi module đang tắt”, trái trực tiếp với bảng phía trên. Bảng còn cho phép PWRKEY khi QPOWD không được, nhưng đoạn shutdown lại cấm thao tác này khi hết deadline. Phải phân biệt rõ **OFF đã xác nhận** và **trạng thái không xác định**, rồi quy định một đường recovery nhất quán cho trường hợp sau. [§7.2](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:454).

- **PPP — diễn giải mới chưa chính xác:** nhận toàn bộ hoặc từ chối một lần callback **không bảo đảm cả frame PPP được nhận nguyên vẹn**. lwIP có thể gọi callback nhiều lần cho một frame; các đoạn trước đã được nhận khi đoạn sau bị từ chối. Sửa câu giải thích này và thêm test hàng đợi đầy ở callback thứ hai/trở đi, kiểm parser tái đồng bộ. Không cần đổi kiến trúc. [Mã PPPoS chính thức, `pppos_output_append()` và `pppos_output_last()`](https://raw.githubusercontent.com/lwip-tcpip/lwip/master/src/netif/ppp/pppos.c).

**3. Điều kiện tiếp tục**

Trước khi chốt kế hoạch: sửa các mâu thuẫn OTA, modem, câu cấp phát tĩnh và diễn giải PPP nêu trên. Đây là chỉnh sửa hợp đồng cụ thể, không yêu cầu hoàn thành triển khai trước.

Kế hoạch vẫn bắt buộc gồm:

- **P1:** bảng pool/buffer/mailbox và ngân sách từng vùng; linker-map, đo heap/stack và hai kịch bản tải §12.3.
- **P2:** giới hạn công việc, block/backpressure của task RX; bàn giao ADC nhất quán; test flood, starvation và cả cấu hình ADC dự phòng nếu sử dụng.
- **P3:** factory record, loader SPI4, bảo toàn qua reset/OTA; handoff `degraded/rec_fail`, gồm quy tắc khởi tạo, xóa bộ đếm và kiểm mất nguồn/reset.

Chưa có kết luận PASS cho firmware hoặc HIL từ vòng duyệt tài liệu này.