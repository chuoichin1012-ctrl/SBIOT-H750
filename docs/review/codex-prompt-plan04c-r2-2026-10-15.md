Tiếp tục trong cùng phiên. Đã sửa theo review 04c trong commit `df75cc8`, xem `git diff a1182af..df75cc8 -- stm32/`:
- **`provision.py`:**
  - `pubkey` và `record` tính xong dữ liệu đầu ra trước;
  - từ chối nếu đầu ra trùng tệp khóa (`os.path.samefile`, bắt cả symlink lẫn hardlink);
  - khóa sai thì bị từ chối trước khi đụng tới đầu ra.

  Test mới đã RED trước khi sửa: `pubkey` ghi đè khóa, làm `record` sau đó không đọc được khóa. Giờ `test_provision` đạt 18 kiểm tra.
- **Diễn giải ruling 3, §11.5 bước 3 và comment trong `bind.c`:** CRC sai được *phân loại* là hỏng, không phải bằng chứng.
- mutants 24/24, `test_bind` đạt.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt: còn lỗi Critical hoặc Important nào không? Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
