QUYẾT ĐỊNH: APPROVED WITH CONDITIONS
THỰC THI: NATIVE

Lỗi chặn trong thiết kế bootloader đã được xử lý. Chuỗi workaround khớp errata; script Task 1 khớp **15 + 2** thay thế. Cần sửa bốn điểm sau **trước khi thực thi**:

1. **[Dòng 237](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-02-board-bringup.md:237):** `break` chỉ thoát vòng kiểm phiên bản, rồi vẫn chạy `cp`. Đổi thành cơ chế kết thúc toàn bước với mã lỗi.

2. **[Dòng 1704](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-02-board-bringup.md:1704):** `board.openocd()` chỉ trả stdout và bỏ qua exit code. Phép thử OpenOCD không kết nối target của tôi cho thấy `echo` ghi vào **stderr**. Cần thu cả hai luồng, kiểm mã thoát và truyền lỗi về test.

3. **[Test hostile](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-02-board-bringup.md:2891):** sau ABORT rời memory-mapped cũng phải áp dụng workaround §2.7.4 bằng code chạy trên MCU, bảo đảm cửa sổ timing. Đồng thời kiểm lỗi/hoàn tất lệnh và **assert `SR1 & 1` trước reset**; hiện chỉ in SR1, nên WIP=0 vẫn có thể được báo đạt. [ES0392 §2.7.4](/home/linh/projects/SBIOT-Gateway/docs/es0392-stm32h742xig-stm32h743xig-stm32h750xb-stm32h753xi-device-errata-stmicroelectronics.pdf).

4. **[Global Constraints, dòng 52](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-02-board-bringup.md:52):** vẫn ghi PLL1 **Q=4**; sửa thành **Q=6** cho thống nhất.

Sau các sửa này, có thể thực thi Native và chạy đầy đủ gate trên board. Tôi chưa chạy lại bench.