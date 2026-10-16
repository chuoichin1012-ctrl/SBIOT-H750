D1: **đồng ý** — tách 09b về hành vi và 09c về RAM. Chưa nâng giới hạn node trước khi đo đỉnh heap, stack và tải đồng thời.

D2: **khác — chọn (b), kể cả network và system.** Tự dùng mặc định cho hai cấu hình này trái yêu cầu “không tự chuyển cấu hình quản trị”, đặc biệt có thể khôi phục login mặc định ngoài ý muốn. Cấu hình sai thì phần phụ thuộc không chạy; sửa qua web nếu còn truy cập được, nếu không dùng Reload. **Nút Reload và ngõ ra an toàn phải hoạt động độc lập với cấu hình IO hợp lệ.** Phân biệt key chưa tồn tại do nâng cấp với key đã lưu nhưng sai.

D3: **đồng ý** — kiểm cú pháp, loại gốc và giới hạn tài nguyên; không kiểm nghiệp vụ, không merge. Ghi giới hạn chính xác **10.240 / 49.152 byte**, không tính NUL. GET trả bản đã lưu; runtime dùng bản đã kiểm khi boot.

D4: **đồng ý**, với hai chi tiết:
- Bảng default và các chuỗi phải tồn tại suốt vòng đời DB; FlashDB giữ con trỏ tới bảng.
- Reset rồi reboot; chặn các lần lưu mới, kể cả `keepOutputs`, từ lúc reset bắt đầu tới reboot để cấu hình cũ không được ghi trở lại.

D5: **khác ở 09c — giữ kiểm cú pháp trước commit manifest.** Nếu chuyển hết sang boot thì phá cam kết D3: GET có thể trả JSON hỏng, UI không đọc được để sửa. Chọn scanner theo luồng, giữ trạng thái qua các chunk; không parse riêng từng record. Có thể ghi generation mới trong lúc nhận, nhưng chỉ commit khi body đầy đủ và cú pháp hợp lệ. Chia record theo byte là phù hợp, kể cả cắt giữa chuỗi UTF-8.

D6: **đồng ý** — quy định thứ tự cố định **web → port1 → port2**, không phụ thuộc lịch chạy task. Dịch vụ xung đột phía sau không mở listener và báo rõ xung đột; không tự đổi cổng.

D7: **đồng ý** — chưa cần sinh code. Test chéo phải gồm biên số, kiểu dữ liệu sai, quy tắc chéo và mô hình Edge. Bộ nạp C vẫn là nơi quyết định dữ liệu có được dùng hay không; không được âm thầm sửa trường sai thành giá trị hợp lệ.

D8: **đồng ý** — giữ kiểm chứng chỉ và khóa như hiện tại. Đây là kiểm vật liệu bảo mật, không chỉ kiểm toàn vẹn file. Buffer heap tạm phải được giải phóng trên mọi đường thoát; xóa nội dung khóa riêng trước khi giải phóng.

Các rủi ro cần ghi vào plan:

- **Default DB có thể báo thành công dù ghi default lỗi:** bản FlashDB hiện tại bỏ qua kết quả `create_kv_blob` tại [fdb_kvdb.c:1435](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/flashdb/src/fdb_kvdb.c:1435). Nếu giữ quyết định không vá thêm, lớp store cần đọc lại các default trước khi báo reset thành công. [Mã upstream cũng có đường này](https://github.com/armink/FlashDB/blob/master/src/fdb_kvdb.c).
- **Reset nhiều DB không nguyên tử.** Cắt điện có thể để lại trạng thái reset một phần; cần test và ghi rõ người dùng thực hiện Reload lại, không hứa tự rollback.
- **Tương thích Edge cũ:** reader mới phải đọc được các record đã chuẩn hóa của firmware hiện tại; không mặc nhiên yêu cầu người dùng xóa DB khi nâng cấp.
- **OOM khác cấu hình sai.** Báo riêng lỗi đọc/cấp phát và lỗi nội dung. Khi boot thất bại giữa chừng, không khởi chạy một phần mô hình Edge.
- **Bỏ static không đồng nghĩa giảm đỉnh RAM.** Đo cả buffer heap tạm, cây parse và runtime cùng tồn tại; kiểm con trỏ runtime không còn trỏ vào bộ nhớ tạm đã giải phóng.