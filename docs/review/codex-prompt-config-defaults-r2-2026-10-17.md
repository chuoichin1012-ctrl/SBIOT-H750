Review code của branch `config-defaults`, vòng 2. Cả hai lỗi Important của vòng 1 đã sửa (commit 603f5d6, trong worktree `.worktrees/cfg`):

1. **`with_defaults()` gặp lỗi cấp phát:**
   - hàm trả về một trong ba giá trị: FILL_NONE, FILL_DONE, FILL_NOMEM. Kết quả của `cJSON_Duplicate` và `cJSON_AddItemToObject` được kiểm; item thêm không được thì giải phóng;
   - `store_set_text` gặp FILL_NOMEM thì trả `STORE_ENOMEM` trước khi ghi flash;
   - `load()` lúc boot gặp FILL_NOMEM thì phục vụ bản lưu, có ghi log;
   - test: `test_with_defaults_oom` làm hỏng lần lượt từng lần cấp phát. Mỗi lần lưu hoặc thành công và GET đủ key, hoặc trả `STORE_ENOMEM`. ASan bắt rò bộ nhớ.
2. **Chuyển HEX/ASCII làm đổi dữ liệu:**
   - các byte không phải văn bản in được (ví dụ `7E01FF`), hoặc không phải UTF-8, thì `convertData` trả `null`. Khi đó UI giữ nguyên HEX (radio được vẽ lại) và báo lỗi;
   - bộ giải mã chạy với `ignoreBOM`, nên `EFBBBF41` qua lại vẫn giữ đủ byte;
   - test: lần chuyển bị từ chối, và chuyển qua lại cho `EFBBBF41`, `5342494F54`, `E1BBA9`. Đã kiểm trên trình duyệt: giá trị vẫn `7E01FF`, radio vẫn HEX, có thông báo.

`test_store`, `test_store_small` và web unit test (13/13) xanh. Phạm vi: `git -C .worktrees/cfg diff 603f5d6~1 603f5d6 -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important.
