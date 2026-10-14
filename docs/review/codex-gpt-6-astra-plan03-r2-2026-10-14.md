QUYẾT ĐỊNH: APPROVED WITH CONDITIONS
THỰC THI: NATIVE

Tại `428683e`, các sửa OOM, buffer in, Unicode, dừng `emit` và trình tự RED/GREEN đã xử lý các lỗi chặn. Năm quyết định thiết kế được chấp nhận.

Còn hai điều kiện cụ thể, cần bổ sung vào plan và hoàn thành khi triển khai:

1. **Gate bộ nhớ chưa đo đúng đại lượng:** `peak` vẫn chỉ cộng block đang sống, chưa tính control block TLSF và phân mảnh. Vì vậy 41.832 byte chưa chứng minh tổng nhu cầu ≤60 KB. Giữ số này với nhãn rõ ràng; thêm lượt chạy các ca cực đại/biên trong **pool tổng cộng 61.440 byte**, yêu cầu kết quả đúng và không OOM. [Cách tính hiện tại](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-03-edge-pipeline.md:292)

2. **Thiếu test Unicode lỗi trong member bị bỏ qua:** mã scanner kiểm toàn văn bản đúng hướng, nhưng các test `junk` trong plan hiện đều hợp lệ. Thêm ca `junk` chứa surrogate đơn và UTF-8 sai; `cfg_edge_text` phải trả `CFG_INVALID`, thông báo JSON không hợp lệ và **không gọi emit**. [Các ca hiện tại](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-03-edge-pipeline.md:1496)

Không thấy lỗi chặn mới qua đối chiếu tĩnh. Giữ **Native** vì các task phụ thuộc chặt và cùng sửa `cfgmodel`.