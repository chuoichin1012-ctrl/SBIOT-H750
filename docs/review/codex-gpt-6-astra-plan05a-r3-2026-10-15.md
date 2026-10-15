QUYẾT ĐỊNH: NOT APPROVED

Còn lỗi **Important**:

1. **Lỗi đọc header vẫn có thể phá KV** — [fdb_kvdb.c:356](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/flashdb/src/fdb_kvdb.c:356). `read_kv()` bỏ qua lỗi đọc rồi dùng `kv_hdr` chưa hợp lệ; nếu `len` sai, nó ghi `ERR_HDR` vào KV tại dòng 366. Cờ giữ sector của GC không hoàn tác được việc này. Cần dừng ngay khi đọc lỗi, không ghi trạng thái; thêm test hết ba lượt đọc, buffer nhận dở.

2. **Factory reset thất bại làm RAM khác flash** — [store.c:663](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:663). Nếu reset DB `cfg` lỗi, toán tử `||` bỏ qua reset `edge`, nhưng vẫn đặt `s_gen=s_count=0`, trong khi `s_edge_ok` còn đúng. GET trả `[]`; reboot lại thấy model cũ. Cần giữ trạng thái Edge nếu chưa đụng tới nó, hoặc chuyển sang unavailable khi kết quả không rõ; thêm test lỗi reset.

**Ruling 8 chưa chấp nhận như hiện tại.** Ba lần đọc thất bại liên tiếp không chứng minh flash hỏng vĩnh viễn. Khi đọc header sector hết retry, mã vẫn có thể format sector tại [fdb_kvdb.c:1602](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/flashdb/src/fdb_kvdb.c:1602). Có thể không phục hồi được phần cứng hỏng, nhưng cần phân biệt **đọc lỗi** với **đọc thành công và header hỏng**, rồi từ chối mount ở trường hợp đầu.

Lý do bỏ mutant cũng **không đúng**: nhánh commit không rõ còn xảy ra khi mount thành công nhưng đọc manifest lỗi; lúc đó ghi/xóa vẫn có thể hoạt động. Cần khôi phục test cho trường hợp này, không coi mutant là tương đương ([mutants_ota.py:45](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/host/mutants_ota.py:45)).

Binary `test_store` có sẵn chạy đạt khi tắt LeakSanitizer; không rebuild hoặc chạy lại ma trận/mutant. Các phát hiện trên là đối chiếu mã.