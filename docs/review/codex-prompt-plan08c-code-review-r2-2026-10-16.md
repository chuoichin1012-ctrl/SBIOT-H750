Plan 08c, code review r2: các commit 95e8d72..495df43 sửa 5 điểm của r1. Chi tiết nằm ở mục "Code review round 1" trong plan.

1. **Model không đọc được:** `store_edge_text` chỉ trả 0 khi DB `edge` không đọc được (khi chưa lưu model nào nó trả `[]` mặc định). Khi đó Edge tắt, trạng thái `MODEL UNREADABLE`.
2. **Hết bộ nhớ khi biên dịch template:** `edge_tmpl_check` đếm `json_alloc_failures`, và coi NULL khi biên dịch một object JSON hợp lệ là NO_MEMORY.
   - Lúc boot: `give_up("NO MEMORY")`.
   - Lúc lưu `edgeReport`: trả 500, cấu hình cũ giữ nguyên. Lỗi cấp phát khi parse body cũng được tính.
   - Test `test_edge_core` `tmpl_check` quét lỗi cấp phát qua hook của cJSON, và qua `--wrap=malloc/calloc` cho phần cấp phát của edgecore.
3. **Biên 65536:** `edge_batches` dùng `uint32_t start/end/e`. Test `batch_top`: 65530 (uint16) cộng 65534 (float32) gom thành một lần đọc, qty 6.
4. **Framing RTU trên cổng Modbus gateway:**
   - Mỗi client có `eacc` riêng, lấy từ `edge_reserve` (`acc_bytes` = 16 × `mbtcp_acc_t`, chỉ khi có truy cập từ xa qua socket), gắn khi accept/connect và gỡ khi drop/abort.
   - `from_network` cắt request của Edge bằng `eacc` trên mọi cổng, trừ MBAP trên cổng gateway (đi qua `on_adu`). Phần không thuộc Edge đi tiếp `pass_on`, tức tới gateway với `acc` riêng của nó. Xả request dang dở dùng `eacc`.
   - Bench `gwrtu` đạt: RTU chia mảnh và ghép, rồi một request Modbus TCP của bus đi tiếp tới gateway.
5. **`bigreport`:** client dừng đọc 7 s rồi mới gửi request. Câu trả lời phải chờ 9475 byte report, nhiều hơn tổng 8144 byte mà lwIP (5840) và bộ nhận của PC giữ được. Vậy bridge đang giữ một report gửi dở, và report đó vẫn tới nguyên vẹn trước câu trả lời.

**Thay đổi theo yêu cầu của người dùng:** polling interval là thời gian nghỉ sau **mỗi** block (Modbus) hoặc point (DL/T645), rồi mới đọc block kế tiếp (`edge_step`, có test). Đọc lại sau một lệnh ghi từ xa thì đọc cả thiết bị một lượt. Đã cập nhật spec §6.4. Bench `basic` kiểm khoảng cách giữa các request tới cùng một thiết bị.

**Kiểm thử:** host test xanh. Bench `edge_check` đầy đủ qua 76 kiểm tra. Hai ca không qua trong lượt đó (`dead` cadence, `shortreport`) đã chạy lại riêng và đạt: `shortreport` là lỗi của chính test (ghép accept/close sai), đã sửa; `dead` là trễ trên đường TCP qua NAT của WSL. Bộ đếm UART trên board cho thấy không mất frame nào.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi.
