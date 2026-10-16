NOT APPROVED

Cần bổ sung hai điểm nhỏ:

1. **T1 còn bỏ sót test phụ thuộc arena.** [T1:76](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-16-09c-ram.md:76) chỉ nêu bỏ `test_arena`, nhưng `test_text_small.c` và `test_edge_text.c` vẫn gọi `arena_init/begin/end` và liên kết TLSF. Bỏ API arena sẽ làm chúng không build được. Ghi rõ chuyển hai test sang allocator mới, giữ các kiểm tra nội dung/OOM còn phù hợp, bỏ riêng các assertion về pool TLSF; chạy lại hai bộ này.

2. **T3 cần xác định request nào sở hữu phiên ghi.** [Ruling 2:59](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-16-09c-ram.md:59) nói close/timeout/error gọi `store_edge_abort`, nhưng chưa phân biệt POST đang upload với POST thứ hai bị từ chối. Quy định chỉ request đã `begin` thành công được write/commit/abort; dọn dẹp sau commit không được hủy phiên kế tiếp. Thêm ca: A đang upload, B nhận 503 rồi đóng; A vẫn commit được. Một cờ sở hữu trên slot web là đủ, không cần cơ chế phức tạp.

Các quyết định RAM, scanner, buffer tạm và phép đo còn lại phù hợp.