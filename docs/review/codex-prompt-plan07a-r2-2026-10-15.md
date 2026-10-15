Vòng 2 cho kế hoạch 07a. Đã sửa 3 điểm của vòng 1, xem mục "Codex round 1" cuối plan.

1. **RTU:**
   - tại RTO (sau t1.5 im lặng) ghi lại vị trí DMA và đặt one-shot cho t3.5 − t1.5;
   - khi hết hạn: vị trí DMA đã đổi, hoặc USART `BUSY` (ký tự đang nhận dở), thì là vi phạm;
   - vi phạm thì bỏ frame và đồng bộ lại: bỏ mọi byte cho tới khi có đủ t3.5 im lặng;
   - có test cho các trường hợp trên và cho 1000 RTO liên tiếp.
2. **TX:**
   - XOFF dừng cả DMA đang phát bằng `CR3.DMAT`;
   - deadline, hoặc DMA lỗi: abort, trả lỗi, nhả mutex; lần ghi sau vẫn chạy;
   - có test cho từng trường hợp.
3. **Bench 1200 8E1** (t1.5 = 13,75 ms, t3.5 ≈ 32,08 ms):
   - ca vi phạm chỉ PASS khi bộ đếm vi phạm của board tăng;
   - ca đối chứng: gap 60 ms cho ra hai frame;
   - thêm ca XOFF/XON.

Ngoài ra: DE vốn là phần cứng của USART (DEM, chân `USARTx_DE`); đã sửa câu dễ hiểu nhầm về TC.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách việc phải sửa.
