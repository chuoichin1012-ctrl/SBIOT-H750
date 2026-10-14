Vòng 5, tiếp tục trong cùng phiên. Các điểm vòng 4 còn mở (S1, S6, câu của S7, diễn giải PPP) đã sửa trong commit mới nhất. Xem `git diff 94db552 HEAD -- stm32/docs/specs/`.

- **S1, §11.2:** toàn bộ quy tắc được gom thành **một bảng quyết định duy nhất**, 9 hàng loại trừ nhau, xét từ trên xuống. Ghi bản ghi trạng thái thất bại được tách thành các hàng 2–5. Riêng hàng 5 (QSPI đang dở và `rec_fail` ≥ 3) phục hồi không có checkpoint, theo dõi tiến độ trong RAM. Có hai chế độ cứu hộ:
  - *có thử lại*: cứ 60 s thử phục hồi một lần, nên mất nguồn liên tiếp không bị coi là lỗi phần cứng;
  - *dừng hẳn*: chỉ khi không còn nguồn hợp lệ nào.

  Gate ở §12.1 yêu cầu mỗi hàng có ít nhất một ca test. Hai bảng trạng thái ở §11.2 đều trỏ về bảng quyết định này.
- **S6, §7.2:** firmware theo dõi ba trạng thái `ON`, `OFF` (đã xác nhận bằng `POWERED DOWN`) và `UNKNOWN`. Thứ tự khôi phục: RESET_N, rồi PWRKEY 2,5 s (nằm giữa ngưỡng bật ≥ 2 s và ngưỡng tắt ≥ 3 s), không bao giờ dùng xung ≥ 3 s. Bench phải xác nhận xung 2,5 s không làm tắt module đang bật.
- **S7, §5.3:** câu về cấp phát tĩnh giờ ghi rõ các ngoại lệ cấp từ heap.
- **PPP:** một frame PPP có thể đi qua nhiều lần gọi callback; frame cụt được phát hiện qua FCS rồi tái đồng bộ. Có thêm test cho trường hợp hàng đợi đầy ở lần gọi thứ hai trở đi.

CHỈ đọc, trả lời ngắn bằng tiếng Việt. Dòng đầu tiên phải là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`. Nếu `APPROVED`, liệt kê những gì kế hoạch triển khai bắt buộc phải bao gồm.
