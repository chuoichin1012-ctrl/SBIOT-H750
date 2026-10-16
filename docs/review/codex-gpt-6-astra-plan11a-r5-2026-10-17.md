NOT APPROVED

Còn hai điểm trong đường lưu lý do reset cần ghi rõ:

1. **`.noinit` không bảo đảm dữ liệu đã xuống SRAM4.** App bật D-cache; MPU hiện chỉ đặt 1 KB buffer ADC của SRAM4 là non-cacheable. Bản ghi lý do có thể còn trong cache khi reset. Cần căn chỉnh vùng riêng theo cache line, clean D-cache và `DSB` sau khi ghi — cả khi xóa bản ghi — hoặc đặt vùng đó non-cacheable. Thêm vào gate linker/startup. [CMSIS cache maintenance](https://arm-software.github.io/CMSIS_6/latest/Core/group__Dcache__functions__m7.html).

2. **Chốt thứ tự đọc nguyên nhân và ghi log sau boot.**
   - `report_boot()` hiện xóa cờ `RCC_RSR` ngay đầu `init`; phải chụp cờ trước đó và giữ lại để xét bản ghi.
   - Chỉ phát log cần lưu sau khi store đã mount.
   - Không chỉ chấp nhận cờ IWDG1: khi R20 được lắp, SGM706 có thể reset qua chân reset trước IWDG1 sau khi ngừng cả hai kick. Cần nhận diện trường hợp này bằng bản ghi hợp lệ và cờ reset tương ứng.

Các sửa về chờ hợp lệ, đăng ký slot và atomic đã giải quyết các điểm của vòng trước.