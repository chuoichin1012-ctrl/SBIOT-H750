Vòng 7, tiếp tục trong cùng phiên. Đã sửa đúng điều kiện bạn nêu, commit mới nhất. Xem `git diff 9eb71bf HEAD -- stm32/docs/specs/`.
- **Hàng 4:** ghi bản ghi thất bại mà có `Vq` thì, **ở bất kỳ trạng thái nào** (kể cả WRITE/RESTORE), không xóa QSPI và nhảy vào app với `degraded`.
- **Hàng 5 và 6:** chỉ áp dụng khi không có `Vq`. Đường RAM có ghi chú rằng nó không bao giờ xóa một image QSPI hợp lệ.
- **Test mới:** ghi xong image nhưng cập nhật bản ghi thất bại, ở cả WRITE lẫn RESTORE, kể cả khi chỉ QSPI còn hợp lệ.

CHỈ đọc, trả lời thật ngắn bằng tiếng Việt. Dòng đầu tiên phải là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`, tiếp theo là danh sách những gì kế hoạch triển khai bắt buộc phải bao gồm.
