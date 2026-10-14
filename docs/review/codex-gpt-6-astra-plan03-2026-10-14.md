QUYẾT ĐỊNH: NOT APPROVED
THỰC THI: NATIVE

Hướng thiết kế phù hợp, nhưng code trong plan còn lỗi chặn:

1. **OOM có thể trả thành công với dữ liệu sai/thiếu.** `arena.failed` được đặt nhưng hai entry point không kiểm; `take()`/`object()` bỏ qua lỗi thêm/thay field của cJSON. Ví dụ, cấp phát giá trị mới thất bại thì giá trị mặc định có thể còn nguyên mà hàm vẫn thành công. Đây là suy luận từ luồng mã, vi phạm §9.3. Phải xử lý lỗi cấp phát trước khi tiếp tục dùng cây; thêm fault injection tại **từng allocation** cho cả hai entry point, kiểm không crash, không `CFG_OK` thiếu trường. Test arena 12 KB chưa đủ. [Plan:996](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-03-edge-pipeline.md:996), [take/object](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/cfgmodel/cfgmodel.c:272)

2. **Buffer `10240+1` từ chối sai output đúng 10.240 byte.** `cJSON_PrintPreallocated` cần phần dư khi ước lượng; header chính thức của bản vendored yêu cầu dự phòng 5 byte. Tách dung lượng buffer khỏi giới hạn chấp nhận, rồi kiểm chiều dài thực. Thêm test output **10.239/10.240/10.241 byte**. [Plan:1002](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-03-edge-pipeline.md:1002), [cJSON.h:168](/home/linh/projects/SBIOT-Gateway/stm32/firmware/third_party/cjson/cJSON.h:168)

Các điểm khác cần sửa:

- **Scanner chưa thực hiện đầy đủ cam kết Unicode:** chấp nhận byte UTF-8 sai; `"\uD800"` qua scanner nhưng cJSON từ chối, rồi bị báo nhầm OOM. Chốt chính sách surrogate, kiểm UTF-8 và thêm test cả key/value, kể cả member bị bỏ qua. Đồng bộ spec về miền tương đương và giới hạn sâu 32. [Scanner](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-03-edge-pipeline.md:584), [RFC 8259 §8–9](https://datatracker.ietf.org/doc/html/rfc8259#section-8)
- **`emit=false` chưa dừng xử lý:** các lời gọi `out_put()` bỏ qua kết quả; lỗi schema phía sau có thể đổi `CFG_ABORTED` thành `CFG_INVALID`. Dừng và cleanup ngay; thêm test sink thất bại giữa luồng, phía sau có lỗi schema. [Plan:1627](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-03-edge-pipeline.md:1627)
- **Gate bộ nhớ chưa đủ:** `peak` chỉ cộng block đang sống, chưa tính control block TLSF và phân mảnh; phần lớn test cấu hình nhỏ chạy ngoài arena. Bổ sung đo trên pool thật cho cực đại từng cấu hình, chuỗi escape và round-trip. [Plan:285](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-03-edge-pipeline.md:285)
- **Bước thực thi:** Task 1/2 chưa tạo file implementation nên bước đỏ sẽ báo thiếu target, không phải lỗi linker như ghi. Cuối plan cũng dẫn mutant nhầm Step 5 thay vì Step 6.

Về năm quyết định: **(1), (2), (5) chấp nhận**; **(4) chấp nhận về thiết kế**, cần sửa buffer/OOM như trên; **(3) chấp nhận việc siết cú pháp và sâu 32**, nhưng cần hoàn thiện chính sách Unicode và cập nhật spec. Chọn **Native** vì bốn task phụ thuộc chặt, cùng sửa `cfgmodel` và bộ oracle.