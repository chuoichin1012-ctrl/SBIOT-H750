QUYẾT ĐỊNH: NOT APPROVED

Còn **2 lỗi Important**:

1. **`store_state_get()` vẫn báo thành công khi đọc lỗi** — [store.c:738](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:738). `fdb_kv_get_blob()` trả độ dài dù lần đọc giá trị thất bại. Sau ba lần lỗi, buffer có thể chứa dữ liệu rác nhưng API vẫn báo đã đọc đủ trạng thái DO. Khóa FAL chỉ chặn ghi/xóa. Cần kiểm lỗi sau đọc, trả 0 và thêm test lỗi tại lần đọc giá trị `st.do`.

2. **Edge có thể được bật lại sau khi storage đã chuyển chỉ đọc** — [store.c:290](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:290), [377](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:377). Lỗi trước `edge_mount()` đặt `s_ro=true`; mount sau đó đặt lại `s_edge_ok=true`, còn `read_only()` không vô hiệu hóa Edge nữa. Với DB Edge chưa có model, GET vẫn trả `[]`, trái §9.1. Cần bảo đảm `s_ro` luôn kéo theo Edge unavailable; test lỗi trước mount Edge phải kiểm riêng `pin.count==0` và không trả model.

Thiết kế khóa ghi/xóa tại FAL hợp lý và đã bảo vệ dữ liệu flash trong các đường được đối chiếu.

Hai binary có sẵn `test_store` và `test_store_small` chạy đạt khi tắt LeakSanitizer. Không rebuild hoặc chạy lại toàn bộ ma trận/mutant; hai phát hiện trên dựa trên đọc mã.