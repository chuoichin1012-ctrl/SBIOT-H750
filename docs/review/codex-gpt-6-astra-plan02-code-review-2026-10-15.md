QUYẾT ĐỊNH: APPROVED WITH CONDITIONS

Review tại `6746c5a`, không gồm sửa đổi chưa commit.

**Critical:** Không phát hiện.

**Important:**

- [board.py:49](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/bench/board.py:49): `listen()` không chờ thread `during()` kết thúc. Nếu OpenOCD vượt cửa sổ 6 giây, ca hostile tiếp theo có thể bắt đầu trong khi ca trước vẫn điều khiển board. Đã tái hiện bằng mock chỉ trong RAM. Cần quản lý thread/process, timeout và truyền lỗi về; không bắt đầu ca tiếp theo khi thao tác trước chưa kết thúc.

**Minor:**

- [flash.sh:7](/home/linh/projects/SBIOT-Gateway/stm32/firmware/tools/flash.sh:7): tham số sai như `ap` bỏ qua nạp nhưng vẫn reset board và báo thành công. Kiểm whitelist `boot|app|both|blank` trước mọi thao tác.
- [boot_check.py:103](/home/linh/projects/SBIOT-Gateway/stm32/firmware/test/bench/boot_check.py:103): khoảng thời gian đo bắt đầu từ thông báo ngừng kick, nhưng nhãn ghi “after the last kick”; hai mốc có thể lệch gần 200 ms. Đổi nhãn hoặc ghi timestamp lần kick cuối.

**Điều kiện trước merge:** sửa mục Important, kiểm trường hợp callback chậm/thất bại, và bổ sung kết quả `--cold` đạt trên revision cuối.

Các ruling nêu trong ledger chấp nhận được. `check_pins` trên bản HEAD, `check_schema`, `test_board` và 28 kiểm tra ELF đều đạt khi kiểm lại; test/ELF dùng artifact sẵn có, không build lại, `test_board` tắt leak detection. Chưa xác nhận kết quả boot nguội.