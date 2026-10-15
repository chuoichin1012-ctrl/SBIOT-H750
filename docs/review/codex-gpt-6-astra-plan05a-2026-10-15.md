QUYẾT ĐỊNH: NOT APPROVED

Còn các lỗi **Important**, xác định qua đường đi trong mã:

1. **Có thể xóa generation đã commit** — [store.c:460](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:460). FlashDB có thể ghi manifest mới thành công rồi báo lỗi khi xóa manifest cũ ([fdb_kvdb.c:1320](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/flashdb/src/fdb_kvdb.c:1320)). Store lập tức xóa generation mới, khiến manifest trỏ tới dữ liệu đã mất. Cần giữ dữ liệu khi kết quả commit chưa xác định; thêm test lỗi một lần tại thao tác xóa manifest cũ.

2. **Lỗi đọc/OOM manifest có thể dẫn tới mất model** — [store.c:224](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:224). Lỗi bị coi như chưa có manifest, đặt `s_gen=0`. Nếu manifest thật đang là generation 1, POST tiếp theo xóa generation 1 trước validation; ngay cả POST sai JSON cũng phá model. Cần phân biệt “không tồn tại” với lỗi đọc/OOM và chặn ghi khi chưa biết trạng thái.

3. **Hai vòng lặp có thể giữ khóa vô hạn:**
   - [fdb_kvdb.c:1613](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/flashdb/src/fdb_kvdb.c:1613): bản vá luôn quét lại dù chuyển `PRE_WRITE → ERR_HDR` thất bại hoặc ghi im lặng không đổi dữ liệu.
   - [store.c:207](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:207): có ít nhất 16 record cần dọn nhưng xóa liên tục thất bại thì cứ gặp lại cùng nhóm record.
   
   Cần kiểm lỗi và tiến độ, thoát có lỗi và nhả khóa. **Bản vá FlashDB đúng hướng nhưng chưa đủ.**

4. **Chưa hỗ trợ chứng chỉ UART2** — [store.c:610](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/store/store.c:610) chỉ nhận `mqtt`, `port1`; `port2` bị từ chối, trái spec §10.1. Thêm scope và test CA/cert/key qua reboot.

**Minor:** [spec §5.3](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:149) chưa cộng buffer mới: phần tĩnh khoảng **193 KB**, phần AXI chưa cấp khoảng **63 KB**, thay vì 185/71 KB.

**Bảy ruling:** chấp nhận về lựa chọn thiết kế; ruling 2 cần sửa phép cộng RAM trên. Cần bổ sung các test lỗi đọc, OOM, lỗi ghi sau commit và ghi không tiến triển nêu trên; ma trận cắt điện hiện tại không thay thế chúng.

Tôi chạy các binary có sẵn: `test_store`, bản cache nhỏ và lượt `STORE_CUTS=1` đều đạt. Không rebuild; phải tắt LeakSanitizer vì sandbox không hỗ trợ. Chưa tái hiện các lỗi trên bằng test mới.