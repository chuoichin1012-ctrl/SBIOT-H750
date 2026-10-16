NOT APPROVED

Còn một mâu thuẫn ở [plan:123](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-16-08c-edge.md:123): quy tắc yêu cầu **200 ms không có byte mới**, nhưng timestamp lại lấy từ **byte đầu tiên**.

Ví dụ nhận các đoạn tại 0 ms và 150 ms: tính từ byte đầu sẽ chuyển tiếp request dang dở ở 200 ms, dù mới im lặng 50 ms.

**Sửa:** cập nhật timestamp khi nhận thêm byte; thêm test tại 210 ms vẫn giữ, tới 350 ms mới chuyển tiếp nếu không nhận thêm.

Bốn điểm còn lại đã được xử lý đủ trong kế hoạch.