QUYẾT ĐỊNH: APPROVED WITH CONDITIONS
THỰC THI: NATIVE

Roadmap chia hợp lý; viết chi tiết từng sub-plan theo code thực tế là phù hợp. **P1–P3 và toàn bộ gate vòng 7 đã có nơi nhận**. Cần sửa các điểm sau trước khi thực thi:

1. **Roadmap:** chuyển gate `audit_keys.py = 0` từ Plan 08 sang Plan 11, sau MQTT và LTE; công cụ phải quét cây STM32. Ghi rõ Plan 11 phụ thuộc **09 và 10**. Đồng thời thống nhất quy tắc “duyệt xong mới bắt đầu kế tiếp” với câu cho phép 03/04 chạy song song. [Roadmap](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-00-roadmap.md:8)

2. **Phân bổ chức năng:** chưa thấy đầu việc rõ cho `now_us()`, quản lý giờ/NTP, Reload và `idleRestart`, dù các mục spec tương ứng được dẫn chung. Gán chúng vào sub-plan cụ thể. Làm rõ “full API, mọi trang round-trip” ở Plan 06 là cấu hình; nghiệm thu hành vi dịch vụ cần các plan sau. Không cần viết chi tiết các plan đó ngay. [Roadmap](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-00-roadmap.md:15), [spec](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:106)

3. **Snapshot ESP32:** kiểm checksum cộng so danh sách file là đúng, phát hiện sửa/xóa/thêm trong phạm vi được chọn. Nhưng đang loại `firmware/managed_components/*` và toàn bộ `firmware/build*`, nên chưa chứng minh được cam kết “toàn cây không đổi”. Bổ sung nguồn trong `managed_components`; chỉ loại các thư mục sinh tự động được xác định rõ, và ghi đúng phạm vi bảo đảm. [Plan 01](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-01-host-core-and-web.md:54)

4. **Review Focus về `cfg_load`:** test hiện chỉ kiểm `telnet` biến mất; chưa assert `telnetPort`, chưa thử nạp cấu hình port cũ chứa `rfc2217`. Bổ sung các ca đó, đồng thời kiểm trường hợp lệ vẫn được giữ. [Test dự kiến](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-01-host-core-and-web.md:269)

5. **Mock TLS:** chỉ chọn TLS 1.2 chưa làm xuất hiện trường upload vì mặc định `tlsVerify = 0`. Step 6 phải chọn thêm **Verify server**, kiểm CA xuất hiện và upload dùng scope `port2`; chọn xác minh hai phía để kiểm cert/key. [Bước kiểm](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-01-host-core-and-web.md:479), [điều kiện hiển thị](/home/linh/projects/SBIOT-Gateway/src/components/CertUpload.tsx:14)

**`www`: đạt.** Ignore được thêm trước khi build và lệnh stage dùng đường dẫn cụ thể. Reviewer cuối cần xác nhận không có file `www` trong index/commit. [Plan 01](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-01-host-core-and-web.md:155)

Chọn **Native** vì ba task phụ thuộc tuần tự và cùng thay đổi schema/UI; một reviewer độc lập kiểm toàn bộ cuối cùng là phù hợp. Tôi chỉ đối chiếu tĩnh tại `94accde`, không chạy lại scratchpad hay build/test.