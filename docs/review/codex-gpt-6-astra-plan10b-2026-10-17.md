NOT APPROVED

Cần bổ sung hai điểm; các ruling còn lại hợp lý:

1. **T2 phải bao phủ cả PPP thẳng.** Hiện [lte.c:592](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte.c:592) hết 90 s chờ đăng ký và [lte.c:611](/home/linh/projects/SBIOT-Gateway/stm32/firmware/components/netmgr/lte.c:611) hết 2 phút đều dẫn tới RESET, không qua `lte_supervise`. Chỉ đổi hàm này sẽ để đường dự phòng tiếp tục reset liên tục. Ghi rõ dùng chung backoff cho cả hai đường, giữ bộ đếm qua CFUN/RECOVER/power cycle; thêm ca ép fallback.

2. **Thêm ca không nhận `POWERED DOWN`.** T2 hiện chỉ kiểm power cycle thành công. Cần kiểm `power_down()` thất bại thì không gọi `lte_power_up(LTE_OFF)`, mà đi qua UNKNOWN/RECOVER như quy định. Có thể giả lập kết quả để kiểm, không cần thêm cơ chế phục hồi.