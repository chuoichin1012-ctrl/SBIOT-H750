Tiếp tục trong cùng phiên. Đã áp dụng 5 điều kiện cho roadmap và Plan 01. Xem `git diff 94accde HEAD -- stm32/docs/plans/`.

1. Gate `audit_keys` chuyển sang Plan 11. Plan 11 phụ thuộc 09 và 10. Quy tắc mới: một plan được bắt đầu khi mọi phụ thuộc của nó đã được duyệt, và các plan đủ điều kiện có thể chạy song song.
2. Phân bổ thêm:
   - `now_us()` vào Plan 02;
   - SNTP, `POST /api/time` và trạng thái giờ vào Plan 06;
   - nút Reload vào Plan 07;
   - `idleRestart` và `bufferOffline` vào Plan 08.

   Ghi rõ round-trip ở Plan 06 chỉ là cấu hình.
3. Snapshot giờ bao gồm `managed_components` và mọi file nguồn/cấu hình. Chỉ loại `firmware/build/`, `build-qemu/`, `test/host/build/` và `__pycache__`. Phạm vi bảo đảm được ghi rõ.
4. Thêm test `cfg_load` cho `port1` có `rfc2217`, và assert `telnetPort` biến mất, các key hợp lệ được giữ. Đã chạy thử: trước khi sửa `cfgmodel` có 9 check fail, sau khi sửa thì xanh.
5. Bước kiểm mock ở Step 6 làm theo đúng điều kiện hiển thị của `CertUpload`: None, rồi Verify server, rồi Verify both, kèm kiểm request `scope=port2&kind=ca`.

CHỈ đọc, trả lời thật ngắn bằng tiếng Việt. Dòng đầu tiên là `QUYẾT ĐỊNH: <APPROVED | APPROVED WITH CONDITIONS | NOT APPROVED>`.
