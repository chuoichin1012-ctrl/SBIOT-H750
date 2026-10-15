QUYẾT ĐỊNH: NOT APPROVED

Review tại `c3b0e2a`. Hai điều kiện vòng trước đã hoàn tất, nhưng còn một lỗi cần sửa trước merge.

**Critical:** Không phát hiện.

**Important:**

- [install.c:24](/home/linh/projects/SBIOT-Gateway/stm32/firmware/boot/install.c:24): lỗi đọc image (`IMG_EREAD`) bị coi và cache như image hỏng. **Kịch bản:** chỉ QSPI chứa image CONFIRMED hợp lệ; đọc header lỗi thoáng qua một lần, backup/staging trống → hàng 1 trả `INSTALL_RESCUE_HALT`. Lỗi đọc đã hết nhưng board không tự thử lại. Cần phân biệt **chưa đọc được** với **đã kiểm và không hợp lệ**, cho reset/thử lại khi lỗi I/O; thêm test lỗi đọc một lần với QSPI hoặc backup là nguồn hợp lệ duy nhất.

**Minor:**

- [test_install.c:1007](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/host/test_install.c:1007): nhóm “cắt ba lần” không kiểm số lần cắt thực tế. Nếu lần đầu cắt upload trước PENDING, boot tiếp chạy CONFIRMED cũ, không ghi/xóa nữa nên hai lần còn lại không xảy ra. Cần đếm và assert đủ ba lần, hoặc báo đúng “tối đa ba lần”.
- [install.c:166](/home/linh/projects/SBIOT-Gateway/stm32/firmware/boot/install.c:166): record không đọc được và cả ba image đều hỏng vẫn đi qua hai lượt reset trước khi dừng. Chưa đúng ưu tiên hàng 1 của §11.2; kiểm hết nguồn trước nhánh tăng lượt phục hồi.

`test_ota` và `test_install` chế độ quick đạt trên binary sẵn có; tôi không build lại hoặc chạy lại toàn bộ ma trận. Ruling `.gitignore` chấp nhận được.