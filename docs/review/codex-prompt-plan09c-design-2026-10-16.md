Câu hỏi thiết kế cho Plan 09c (RAM), theo mục 6 trong yêu cầu của người dùng:

> "Tối ưu RAM: bỏ arena tĩnh khi đã loại bỏ các phụ thuộc, chuyển nhận/đọc cấu hình sang buffer nhỏ; đo lại RAM trước khi nâng giới hạn node."

Plan 09b đã merge (PR #17). Người dùng muốn **giữ thật đơn giản**: thiết bị chạy với cấu hình được cung cấp, và không cần tự xử lý mọi lỗi.

Xin trả lời từng mục D1–D7: đồng ý, hoặc chọn khác và nói lý do.

**Hiện trạng** (AXI đã dùng 491 KB / 512 KB; heap 256 KB, thấp nhất khoảng 142 KB khi chạy model Edge lớn nhất cùng MQTT):
- **Arena TLSF 64 KB** (`s_web_arena`): mọi API request chạy trong một scope arena. Sau 09b, lúc lưu không còn parse nữa; cJSON chỉ còn dựng `/api/status`, đọc body của `/api/io` và `/api/time`, đều nhỏ. `/api/status` có trường `arenaPeak`.
- **`s_edge_body` 48 KB** có ba việc:
  - nhận body `POST edgeDevices`, sau đó `store_edge_save` kiểm cú pháp trên toàn bộ body rồi cắt thành record 8 KB;
  - nhận chứng chỉ (12 KB): kiểm PEM, rồi kiểm bằng mbedTLS;
  - lúc boot, `web_edge_buffer_for_boot` cho `edge_rt` đọc cả model vào đó để chạy `cfg_edge_load`.
- **`s_record` 8 KB:** GET `edgeDevices` stream từng record một.

**Quyết định:**

**D1.** Bỏ arena TLSF cùng `s_web_arena`. Mọi cấp phát của cJSON đi vào heap, qua allocator đếm lỗi (`json_alloc_install` đã được cài trước `store_init`). Bỏ `third_party/tlsf` khỏi build; component arena chỉ còn bộ đếm. Bỏ `arenaPeak` khỏi `/api/status`.

**D2.** `POST edgeDevices` stream thẳng vào record, không cần buffer 48 KB. Store có thêm ba hàm `store_edge_begin(len)`, `store_edge_write(chunk)` và `store_edge_commit()`:
- dữ liệu được ghi vào generation g+1 ngay trong lúc nhận, qua `s_chunk` 8 KB;
- một **scanner cú pháp theo luồng**, mới viết trong `proto/jsonscan`, chạy song song. Nó là một máy trạng thái giữ trạng thái qua các chunk, với cùng miền như `js_parse_root`: RFC 8259, UTF-8 hợp lệ, surrogate theo cặp, độ sâu ≤ 32, kiểu gốc là array;
- chỉ commit manifest khi body đủ `Content-Length` và cú pháp hợp lệ. Nếu không thì xóa các record của generation g+1 (đường xử lý sẵn có).

Đây là ý của bạn ở D5 của câu hỏi thiết kế 09b. Host test: scanner theo luồng phải cho cùng kết quả với `js_parse_root` trên toàn bộ bộ mẫu của `test_jsonscan`, với body bị cắt ở **mọi** vị trí byte.

Phương án khác, đơn giản hơn: một buffer heap 48 KB tạm cho mỗi lần lưu. Giảm được RAM tĩnh, nhưng không giảm đỉnh. Khuyến nghị: dùng scanner theo luồng.

**D3.** Lúc boot, model Edge được đọc vào **buffer heap tạm** (tối đa 48 KB), chạy `cfg_edge_load`, rồi giải phóng. Đây đúng là ý của người dùng: "nạp runtime rồi giải phóng bộ nhớ tạm". Có kiểm rằng runtime không giữ con trỏ nào trỏ vào buffer đó: test ASan poison buffer sau khi giải phóng. Không nạp theo từng record, vì một node có thể nằm vắt qua ranh giới hai record.

**D4.** Chứng chỉ được nhận vào một buffer heap 12 KB + 1, chỉ trong lúc upload. Với khóa riêng, xóa nội dung (`mbedtls_platform_zeroize`) trước khi giải phóng, trên mọi đường thoát.

**D5.** GET `edgeDevices` giữ `s_record` 8 KB tĩnh, vì đọc từng phần của một blob FlashDB phải can thiệp vào struct của nó. Khuyến nghị: giữ, cho đơn giản.

**D6.** Khoảng 112 KB AXI được giải phóng: đưa vào heap (heap 256 → khoảng 352 KB, vẫn là vùng `heap_5` duy nhất), vì các buffer tạm giờ đều nằm trong heap. Cập nhật spec §5.3.

**D7.** Đo trên board, rồi ghi vào plan:
- (a) dung lượng tĩnh từng vùng;
- (b) heap free và heap thấp nhất, lúc boot với model lớn nhất (100 điểm);
- (c) như (b), cộng MQTT TLS, port1 TLS và một request HTTPS;
- (d) như (c), cộng một lần lưu `edgeDevices` 48 KB và một GET chạy song song.

Chưa nâng giới hạn node trong 09c. Plan ghi lại con số tính được, ví dụ mỗi điểm thêm tốn bao nhiêu và còn bao nhiêu chỗ, để người dùng quyết định sau.

Trả lời dạng `D1: đồng ý` hoặc `D1: khác — …`, kèm các rủi ro bạn thấy.
