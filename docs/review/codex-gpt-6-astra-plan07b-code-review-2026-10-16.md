NOT APPROVED

Có **2 lỗi Important** cần sửa:

1. [api.c:266](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/webapi/api.c:266): ép chỉ số DO từ `double` sang `int` trước khi kiểm. `{"do":-0.5,"value":true}` bật DO1; `1.9` bật DO2 thay vì trả 400. Kiểm chỉ số hữu hạn, nguyên và trong phạm vi trước khi ép; thêm test xác nhận request sai không đổi relay.

2. [io_core.c:16](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/iomgr/io_core.c:16): `sscanf` chấp nhận `"08:30bad"` hoặc `"08:30:xx"` thành 08:30:00. Schema chỉ giới hạn độ dài, nên cấu hình sai vẫn có thể gây restart hoặc đổi DO theo lịch. Cần kiểm hết chuỗi `HH:MM[:SS]`, thêm test hậu tố và phần giây sai.

Không thấy lỗi chặn khác đã kiểm chứng trong ADC, persistence, khóa latch hay Reload. Ba binary host hiện có chạy đạt. Đo ADC thật trên board có VREF và regression serial vẫn là các kiểm chứng còn chờ đã nêu.