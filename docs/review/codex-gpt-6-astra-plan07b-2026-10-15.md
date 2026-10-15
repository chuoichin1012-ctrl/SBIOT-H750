NOT APPROVED

Cần sửa các điểm sau trong plan:

1. **Bàn giao ADC — [dòng 57](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-07b-io.md:57):** `seq_before`, `seq_now` truyền bằng giá trị đều được lấy trước khi hàm cộng mẫu chạy, nên không phát hiện được ISR đổi `seq` trong lúc cộng. Tách bước cộng và kiểm tra sau cộng, hoặc đọc `seq` qua con trỏ/callback. Test phải đổi `seq` **trong lúc cộng**, không chỉ truyền hai số khác nhau.

2. **Thứ tự lưu `keepOutputs` — [dòng 116](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-07b-io.md:116):** timer đặt ON, nhả mutex; web đặt OFF và lưu; timer sau đó lưu mask ON cũ. Restart sẽ khôi phục sai. Cần bảo đảm thứ tự lưu hoặc luôn lưu trạng thái mới nhất, kèm test hai caller xen kẽ.

3. **Ruling 1 và 3 mâu thuẫn — [dòng 143](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-07b-io.md:143):** FlashDB chạy đồng bộ trên `io` còn trì hoãn pulse, blink và DI. Bench trước đã ghi nhận save **430 ms**, nên không thể hứa chỉ trễ 0–10 ms hoặc chỉ mất một cửa sổ ADC. Chuyển persistence khỏi đường xử lý IO, hoặc ghi rõ giới hạn chấp nhận khác và kiểm khi flash chậm.

4. **Callback timer và mutex — [dòng 115](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-15-07b-io.md:115):** nếu giữ mutex quanh `io_core_timers`, callback gọi `iomgr_do_set` sẽ lấy lại mutex và deadlock. Ghi rõ: thu thập hành động dưới khóa, thực hiện sau khi nhả khóa.

**Ruling 2, 4, 5 chấp nhận được** với các gate Plan 08/11 đã nêu. Dùng tick cho DO cũng hợp lý sau khi giải quyết điểm 3.