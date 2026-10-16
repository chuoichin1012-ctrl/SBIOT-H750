NOT APPROVED

Còn một lỗi so với ruling 4: **sai kiểu JSON vẫn bị ép kiểu thay vì lấy default** tại [cfgmodel.c:270](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/cfgmodel/cfgmodel.c:270).

Ví dụ:

- `{"enabled":2}` của Edge thành `true`, thay vì default `false`.
- `{"webPort":true}` thành cổng `1`, thay vì default `80`.
- `{"cleanSession":"garbage"}` thành `false`, thay vì default `true`.

Các trường hợp này cũng không ghi log dùng default. Test mới chỉ dùng `"fast"` và `{}`, nên chưa bắt được những giá trị sai kiểu nhưng ép được.

**Cần sửa:** kiểm đúng kiểu trên đường `cfg_load` trước khi gọi `take`; thêm test boolean nhận số/chuỗi và integer nhận boolean/chuỗi số. Có thể giữ hành vi cũ của đường validate làm oracle.

Các sửa về giới hạn danh sách và OOM phù hợp với plan. Đã chạy `test_cfgmodel`: PASS, nhưng chưa phủ lỗi trên; không chạy lại bench.