QUYẾT ĐỊNH: NOT APPROVED

Còn **3 lỗi Important**, xác định qua đọc mã:

1. **GC vẫn có thể làm mất KV khi đọc lỗi** — [fdb_kvdb.c:1053](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/flashdb/src/fdb_kvdb.c:1053). `move_kv()` bỏ qua kết quả đọc nguồn; nếu đọc lỗi nhưng ghi thành công, dữ liệu sai được commit rồi bản gốc bị xóa. Cần kiểm lỗi đọc và giữ nguồn; bổ sung fault injection vào các lần đọc trong GC.

2. **Khôi phục chưa hoàn tất vẫn báo mount thành công** — [fdb_kvdb.c:1633](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/flashdb/src/fdb_kvdb.c:1633), [1684](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/flashdb/src/fdb_kvdb.c:1684). Khi ghi trạng thái thất bại hoặc hết lượt quét, hàm vẫn trả `FDB_NO_ERR`. Manifest cũ ở `PRE_DELETE` phía sau có thể chưa được phục hồi, khiến store phục vụ model mặc định. Cần trả lỗi mount khi chưa hoàn tất; test phải kiểm kết quả và dữ liệu, không chỉ kiểm hết treo.

3. **Nhánh giữ dữ liệu khi commit chưa rõ vẫn có lỗ hổng** — [store.c:497–511](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:497). Nếu `old=0`, manifest mới đã ghi nhưng báo lỗi, rồi đọc xác minh lỗi nên `g=0`, điều kiện `s_gen == old` vẫn xóa generation mới. Cần trạng thái riêng cho “chưa xác định”, tuyệt đối không cleanup trong trạng thái đó; lỗi mount/đọc cũng không được coi là model rỗng.

Bản vá GC đúng hướng nhưng chưa đủ; giới hạn quét mới cần xử lý lỗi khi dừng.

Binary có sẵn chạy đạt cả lượt thường và `STORE_CUTS=1`, gồm ma trận GC 1.347 thao tác. Tôi không rebuild; tắt LeakSanitizer do sandbox. Các kịch bản trên chưa được tái hiện bằng test mới.