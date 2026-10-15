Tiếp tục trong cùng phiên. Đã sửa theo review r3 trong commit `0260686`, xem `git diff b3b5945..0260686 -- stm32/` và mục "Codex round 3" trong plan doc.

1. **Lỗi đọc: không vá từng chỗ đọc trong FlashDB nữa, mà chặn ở lớp FAL.**
   - Đọc lỗi 3 lần thì FAL từ chối **mọi lệnh ghi và xóa** cho tới lần boot sau (`fal_nor_rearm()` chỉ được gọi trong `store_init`). Nhờ vậy FlashDB không bao giờ ghi hay xóa dựa trên một lần đọc sai: `read_kv` không ghi được `ERR_HDR`, mount không format được sector.
   - `store` chuyển sang chỉ đọc: từ chối lưu (500), edge ở trạng thái không dùng được, không trả chứng chỉ hay bản ghi vừa đọc.
   - Test: thêm ma trận đọc lỗi 3 lần liên tiếp ở bất kỳ đâu kể cả lúc mount, với nửa bộ đệm bị ghi rác (RED 1/3618, rồi GREEN). Thêm ma trận trong lúc boot và trong lúc lưu edge (kể cả lần mount lại). Một lần đọc lỗi đơn lẻ thì store vẫn lưu được.
   - Hai chỗ vá đọc của vòng r2 (`move_kv` kiểm lần đọc, `do_gc` giữ sector) đã bị gỡ, vì mutant của chúng sống sót khi đã có khóa này. File vá FlashDB giờ chỉ còn các lỗi về ghi và khôi phục.
2. **Factory reset:** mỗi DB được reset độc lập. Reset `edge` thất bại thì mount lại `edge`. Cache cấu hình được nạp lại từ flash. Test: reset `cfg` lỗi giữa chừng, và reset `edge` lỗi giữa chừng. Việc mount lại `cfg` và `cert` tỏ ra không cần (mutant sống sót), nên đã gỡ.
3. **Mutant "commit không rõ":** tương đương theo cấu trúc, vì nhánh đó chỉ chạy được khi một lần đọc hoặc ghi đã lỗi, và sau lần đọc lỗi thì FAL chặn mọi lệnh xóa. Lý do có ghi trong `mutants_ota.py`.
4. **Mutant của `store`:** lượt đầy đủ đạt 23/24. Mutant thứ 24 (mount lại `edge` sau factory reset) được thay bằng bản mới, đã kiểm bằng tay là bị bắt. Lượt chạy giờ mất 8,5 phút nhờ dừng sớm.

Yêu cầu, CHỈ đọc, trả lời ngắn bằng tiếng Việt: còn lỗi Critical hoặc Important nào không? Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
