Câu hỏi thiết kế (brainstorming) cho công việc sau Plan 09, theo yêu cầu của người dùng. Xin trả lời từng quyết định D1–D8: đồng ý với khuyến nghị, hoặc chọn khác và nói lý do. Người dùng muốn giữ đơn giản.

## Yêu cầu của người dùng (nguyên văn, dịch sát)

1. Khai báo default KV cho FlashDB, dùng chung một nguồn giá trị mặc định. DB mới được tạo với các cấu hình mặc định; factory reset xóa cấu hình đã lưu và tạo lại default DB bằng `fdb_kv_set_default`.
2. Web UI validate toàn bộ cấu hình, gửi JSON hoàn chỉnh.
3. Gateway chỉ nhận và lưu JSON: kiểm tra xác thực, tên cấu hình, kích thước và lỗi ghi; không validate nghiệp vụ lúc lưu.
4. Sau reboot, gateway parse cấu hình đã lưu, nạp runtime rồi giải phóng bộ nhớ tạm. Bộ nạp vẫn kiểm tra dữ liệu trước khi dùng, để tránh truy cập bộ nhớ sai hoặc vận hành sai.
5. Cấu hình sai thì người dùng tự sửa hoặc restore mặc định. Không thêm chế độ web cứu hộ, không tự chuyển cấu hình quản trị, không pending hay rollback. Không vào được web thì dùng nút Reload để reset cấu hình.
6. Tối ưu RAM: bỏ arena tĩnh khi đã gỡ hết phụ thuộc vào nó, chuyển việc nhận và đọc cấu hình sang buffer nhỏ; đo lại RAM trước khi nâng giới hạn node.

Giữ cơ chế ghi hoàn chỉnh rồi mới cập nhật manifest cho Edge, để không đọc phải dữ liệu ghi dở khi mất điện. Reset cấu hình đưa DB về mặc định; restore firmware là chức năng riêng.

## Hiện trạng (đã đọc code)

- `store_init` mở KVDB với `default_kv = NULL`. Key thiếu thì `load()` dùng `cfg_default()`, tức chuỗi `defaults` trong bảng `cfgdef_t` của `cfgmodel.c`. `env` chỉ chọn config nào tồn tại và một số cận, không đổi giá trị mặc định.
- `store_factory_reset` đã gọi `fdb_kv_set_default` cho cfg, edge và cert, rồi `load_all()` trong RAM.
- **Lưu config nhỏ** (`store_set_text`):
  - parse trong arena 64 KB tĩnh (AXI) của `web`;
  - `cfg_validate_text` merge với bản hiện tại (UI có thể gửi một phần), chuẩn hóa thành text gọn;
  - `listen_check` kiểm chéo port1, port2 và system.
- **Lưu edgeDevices:**
  - body 48 KB trong `s_edge_body` (AXI);
  - `cfg_edge_text` (strict) validate từng node, rồi ghi các record 8 KB đã chuẩn hóa vào thế hệ g+1;
  - manifest `@g,count` commit; xóa thế hệ cũ.
- **Boot:**
  - `load()` gọi `cfg_load`: kiểm lại schema và điền key mà firmware mới thêm vào. Bản lưu không dùng được thì dùng mặc định cho config đó, và ghi log.
  - Edge: `store_edge_text` đọc toàn bộ vào `web_edge_buffer_for_boot` (chính là `s_edge_body`), rồi `cfg_edge_load` chạy từng node.
- **UI:** đã có `FieldDef` với phạm vi và kiểm topic cho từng trường. Chưa có quy tắc chéo (listen port) và chưa có kiểm đầy đủ mô hình Edge (`cfg_edge_text`). UI hiện gửi lại object nó đã GET (đủ trường).
- **Bộ nhớ static trong AXI:** arena 64 KB, `s_edge_body` 48 KB, `s_record` 8 KB. AXI còn khoảng 20 KB.

## Quyết định

**D1. Chia việc.** Khuyến nghị hai plan, làm trước Plan 10 (LTE):
- **09b — luồng cấu hình:**
  - default KV dùng chung bảng của `cfgmodel`;
  - lưu không validate nghiệp vụ;
  - bộ nạp lúc boot kiểm tra;
  - UI validate đủ, gồm quy tắc chéo và mô hình Edge;
  - factory reset.
- **09c — RAM:**
  - bỏ arena;
  - nhận body vào buffer nhỏ, ghi thẳng ra record;
  - boot đọc vào buffer heap tạm rồi giải phóng;
  - đo lại.

  Việc nâng giới hạn node để sau, quyết định từ số đo của 09c.

**D2. Lúc boot, một config đã lưu không qua được bộ nạp.** Các lựa chọn:
- (a) dùng mặc định cho riêng config đó (như hiện nay);
- (b) dịch vụ của config đó không chạy, `/api/status` nêu config và lỗi;
- (c) trộn: bỏ trường sai, giữ trường đúng.

Khuyến nghị (b) cho các dịch vụ (port1/2, mqtt*, edge*, io), vì yêu cầu là "tránh vận hành sai" và "người dùng tự sửa". GET vẫn trả bản đã lưu, để UI hiện ra cho người dùng sửa.

Riêng **network** và **system**: lỗi thì dùng mặc định, ghi lỗi vào log và status, vì gateway phải còn lên mạng. Nếu người dùng muốn khác thì nút Reload vẫn có.

`cfg_load` vẫn điền các key mới bằng mặc định; đó là nâng cấp, không phải lỗi.

**D3. Lưu có kiểm cú pháp JSON không?** Khuyến nghị **có**, kiểm cú pháp bằng `jsonscan` (không cấp phát). Đây không phải validate nghiệp vụ; nó bảo đảm bản lưu luôn parse được, và GET luôn trả JSON. Kiểm thêm loại gốc: object cho config nhỏ, array cho các danh sách. Không merge nữa: body thay thế toàn bộ. Text được lưu nguyên như gửi, có giới hạn 10 KB (Edge 48 KB).

**D4. Default KV.** Khuyến nghị:
- một bảng `fdb_default_kv` dựng một lần trước `fdb_kvdb_init`, từ chuỗi `defaults` của `cfgdef_t` (một nguồn duy nhất);
- DB mới (lần mount đầu) được FlashDB ghi các mặc định;
- key thiếu trong DB cũ (firmware mới thêm config) vẫn lấy từ cùng bảng lúc boot, không ghi xuống;
- factory reset gọi `fdb_kv_set_default` cho cfg, edge (mặc định `edgeDevices` "[]", không manifest) và cert, rồi **khởi động lại**: cấu hình chỉ đọc lúc boot; hiện nay chỉ `load_all` trong RAM.
- Nút Reload làm đúng việc đó.

**D5. Lưu edgeDevices.**
- Không validate node.
- Kiểm cú pháp (D3) trên toàn bộ text, rồi chia **theo byte** thành các record 8 KB.
- Thế hệ g+1 và manifest giữ nguyên.
- GET trả các record nối lại, đúng như đã lưu.
- Bộ nạp lúc boot chạy `cfg_edge_load` strict trên text nối lại; mô hình sai thì Edge không chạy, status nêu node và lỗi.
- Ở 09c, body đi thẳng vào record: kiểm cú pháp chuyển sang boot, hoặc kiểm từng mảnh với một máy trạng thái nhỏ. Xin chọn: khuyến nghị **chuyển sang boot**, vì đơn giản; GET vẫn trả đúng byte đã lưu.

**D6. Kiểm chéo listen port** (`listen_check`) cũng chuyển sang UI và bộ nạp: hai cổng trùng listen port thì cổng sau không chạy và status nêu lỗi.

**D7. Một nguồn quy tắc cho UI và firmware?** Khuyến nghị **không sinh code**. UI viết lại quy tắc bằng TypeScript, có test riêng. Bộ nạp C giữ `cfgmodel` hiện có làm lớp kiểm cuối. Lệch nhau thì hậu quả là "dịch vụ không chạy, status nêu lỗi", không làm hỏng bộ nhớ. Một test chéo trên host chạy các mẫu (hợp lệ và không hợp lệ) qua cả hai.

**D8. Cert upload** hiện kiểm PEM và parse bằng mbedTLS. Khuyến nghị giữ nguyên: đó là kiểm tính toàn vẹn của file, không phải cấu hình. Ở 09c, buffer 12 KB của nó lấy từ heap thay cho `s_edge_body`.

Trả lời theo dạng: `D1: đồng ý` / `D1: khác — ...`, rồi những rủi ro bạn thấy.
