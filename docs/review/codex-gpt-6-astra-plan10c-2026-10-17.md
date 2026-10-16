NOT APPROVED

Cần bổ sung 4 điểm trong plan:

1. **Task 1 — ca test chưa chứng minh được bản sửa.** `rw_prot` là array thì `rwprot_handle()` hiện cũng trả NULL, bất kể sâu 15 hay 300 tầng. Dùng một request `rw_prot` hợp lệ, thêm member chứa dữ liệu lồng sâu; kiểm đúng biên 16/17 và 300 tầng. Ca vượt giới hạn phải đỏ trước khi sửa.

2. **Task 5 — hook `begin` không truyền được lỗi lấy mutex.** Chữ ký `void` chưa đủ để thực hiện nhánh `MB_EX_DEVICE_FAILURE` đã mô tả. Cho `begin` trả kết quả; thất bại thì không đọc cache và không gọi `end`. Thêm test lấy khóa thất bại và lỗi ở giữa vòng đọc: khóa đã lấy phải được nhả đúng một lần. Hàm cần sửa thực tế là `mb_serve`.

3. **Task 6 — cần chốt hợp đồng sở hữu OTA giữa hai task.**
   - Nhận/nhả quyền phải nguyên tử; từ chối trước khi sửa context hay xóa staging.
   - Chỉ chủ hiện tại được release; caller bị từ chối không được giải phóng phiên khác.
   - `begin` thất bại phải trả quyền; upload thành công giữ khóa tới reboot, kể cả khi có cleanup.
   
   Bổ sung test các đường này, nhất là B bị từ chối rồi cleanup trong khi A vẫn upload.

4. **Task 9 — thiếu thời điểm bắt đầu đếm timeout gửi.** Phải bắt đầu khi hàng gửi chuyển từ rỗng sang có dữ liệu, cập nhật khi thực sự gửi được byte, và bỏ mốc khi hàng rỗng. Nếu chỉ cập nhật tại `MG_EV_WRITE`, phản hồi đầu tiên sau thời gian rảnh dài có thể bị đóng trước lần ghi đầu. Thêm test tình huống này cùng ca đứng yên và ca vẫn tiến triển.

Chấp nhận các ruling chưa chạy bench cho serial và NO SIM; không yêu cầu dựng thêm harness cho hai phần đó.