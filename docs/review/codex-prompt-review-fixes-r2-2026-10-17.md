Review r2 của branch `review-fixes` (worktree `/home/linh/projects/SBIOT-Gateway/.claude/worktrees/review-fixes`). Cả hai lỗi Important của r1 đã được xác nhận trên code và đã sửa trong commit mới nhất:

1. **Địa chỉ thiết bị rỗng:** `addr` trong `slaveDefs` có `required: true` (trường của thiết bị IO vẫn disabled, nên `formErrors` bỏ qua). Test: `addr` rỗng và toàn khoảng trắng đều bị từ chối.
2. **Tên điểm trùng:** `modelErrors()` chỉ loại điểm đang kiểm, theo vị trí của nó trong `allNodeNames` (biến `at`), không loại mọi điểm cùng tên. Test: trùng trong cùng thiết bị và giữa hai thiết bị.

Hai test mới fail khi hoàn tác từng chỗ sửa, và pass khi có bản sửa. `test:unit` 23/23, `npm run build`, `test:e2e` đều xanh.

**Phạm vi:** `git diff main HEAD -- stm32` (r1 xem từ e2cca7e tới 62cfd16).

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi).
