QUYẾT ĐỊNH: APPROVED WITH CONDITIONS
THỰC THI: NATIVE

Tại `9bf7768`, các lỗi logic vòng 1 đã được sửa đúng. Chưa thấy lỗi chặn triển khai; còn hai điều kiện cần hoàn tất trước khi kết thúc Task 2:

1. **Test SHA sai ở đường RAM chưa đúng tình huống** — [dòng 2015](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-04a-ota-host.md:2015). `SIM_SILENT` khiến page header không được ghi, phép so sánh đọc lại thất bại ngay; chưa kiểm trường hợp chép xong nhưng SHA đích sai theo §12.1. Thêm hook làm hỏng byte thân **đã được đọc lại**, giữ header hợp lệ; assert `IMG_ESHA`, kết quả REBOOT, không nhảy, và phục hồi được khi bỏ lỗi.

2. **Đồng bộ chú thích với D7:** [dòng 1075](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-04a-ota-host.md:1075) vẫn khẳng định lỗi ghi luôn để record cũ mới nhất; [dòng 2799](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-04a-ota-host.md:2799) vẫn nói trial không được đếm. Cả hai phải ghi rõ trường hợp CRC đã ghi thành công nhưng đọc lại lỗi.

Các sửa đổi về cứu hộ, timeout, đọc record ba kết quả, `setjmp`, CRC handoff và mất điện liên tiếp đạt qua đối chiếu mã. Tôi chưa chạy lại prototype hoặc xác nhận độc lập kết quả 20 mutant.