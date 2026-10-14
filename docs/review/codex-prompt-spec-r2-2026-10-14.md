Bạn là người phê duyệt kỹ thuật cho một spec firmware nhúng. Chủ dự án đã giao cho bạn quyền phê duyệt thay họ. CHỈ đọc, không sửa, tạo hay xóa bất kỳ tệp nào.

Tài liệu cần duyệt: `stm32/docs/specs/2026-10-14-stm32-firmware-design.md` (vòng 2).

Bối cảnh:
- Vòng 1 bạn đã review và nêu 21 phát hiện, trong `stm32/docs/review/codex-gpt-6-astra-spec-2026-10-14.md`.
- Cách từng phát hiện đã được xử lý ghi trong `stm32/docs/review/codex-spec-2026-10-14-followup.md`.
- Lịch sử thay đổi: `git log -p -- stm32/docs/specs/`.
- Sau vòng 1 có thêm hai thay đổi:
  - lệnh CMUX mà nhóm vận hành đang dùng: `AT+CMUX=0,0,5,2048,20,3,30,10,2` (§7.2);
  - **thiết kế mới cho ADC** (§6.6): ADC3 với oversampling phần cứng ×16, timer kích 1 kHz, BDMA vòng vào một region non-cacheable 1 KB của SRAM4, trung bình 100 ms. Chủ dự án yêu cầu dùng oversampling và DMA để giảm tải đọc.
- Nguồn đối chiếu:
  - schematic `docs/Job2.PDF`;
  - mã ESP32 trong `firmware/`;
  - tài liệu chính thức của ST, GigaDevice, Quectel, SGMicro, Modbus, OASIS và 3GPP.
- Các quyết định D1–D12 ở §2 đã được chốt, không mở lại.

Hãy trả lời bằng tiếng Việt, theo các mục sau:

1. **Kiểm từng phát hiện vòng 1:** với mỗi số 1–21, cho biết **Đã xử lý / Xử lý chưa đủ / Sai**. Với hai trường hợp sau, nêu lý do cụ thể, mục của spec, và nguồn.
2. **Review thiết kế ADC ở §6.6** theo RM0433 và datasheet STM32H750:
   - ADC3 có đúng chỉ dùng được BDMA qua DMAMUX2 không;
   - cấu hình oversampling: OVSR, OVSS, chế độ triggered, độ rộng dữ liệu khi dùng DMA;
   - `DMNGT`;
   - timer nào kích được ADC3;
   - giới hạn kernel clock và `BOOST`;
   - hiệu chuẩn offset và linearity;
   - thời gian lấy mẫu với nguồn là LM2904;
   - region MPU 1 KB trong SRAM4;
   - công thức đổi sang mA;
   - cách xử lý OVR và lỗi BDMA.
3. **Lỗi mới** phát sinh trong lúc sửa: mâu thuẫn giữa các mục, tham chiếu chéo sai, con số không khớp (ngân sách RAM, layout flash, giới hạn body), yêu cầu mơ hồ.
4. **Danh sách phát hiện còn lại**, xếp theo Nghiêm trọng / Cao / Trung bình / Thấp. Mỗi phát hiện ghi mục của spec, mô tả, rủi ro, đề xuất sửa.
5. **Quyết định phê duyệt**, chọn đúng một trong ba:
   - `APPROVED`: spec đủ để chuyển sang lập kế hoạch triển khai.
   - `APPROVED WITH CONDITIONS`: được lập kế hoạch, kèm danh sách điều kiện phải sửa trong spec trước hoặc trong lúc lập kế hoạch. Mỗi điều kiện phải cụ thể và kiểm được.
   - `NOT APPROVED`: còn lỗi chặn. Liệt kê các lỗi đó.

   Dòng đầu tiên của câu trả lời phải là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.

Chỉ nêu những gì bạn kiểm chứng được, và ghi nguồn. Ghi rõ khi một nhận định là suy luận.
