Plan 09c r2 sửa đủ 2 điểm của r1:
1. T1 chuyển `test_text_small` và `test_edge_text` sang allocator đếm lỗi trên heap: giữ các kiểm tra nội dung và OOM, chỉ bỏ các assertion về pool TLSF; cả hai bộ được chạy lại.
2. Ruling 2: chỉ request đã `store_edge_begin` thành công mới sở hữu phiên ghi, qua một cờ trên slot web. POST thứ hai nhận 503 và không sở hữu gì, nên khi nó đóng cũng không hủy phiên nào; phần dọn dẹp sau commit không đụng tới phiên kế tiếp. Có thêm ca bench: A đang upload, B nhận 503 rồi đóng, A vẫn commit được.

Trả lời APPROVED hoặc NOT APPROVED.
