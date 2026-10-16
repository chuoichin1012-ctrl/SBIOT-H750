Plan 08c r3 (commit 44a88ca): `mbtcp_acc_t` giờ ghi thời điểm của byte **mới nhất** (mỗi lần push thêm byte đều cập nhật), nên khoảng im lặng được tính từ byte mới nhất. Test `acc_stale`: nhận byte ở 0 ms và 150 ms thì tại 210 ms vẫn giữ, tới 350 ms mới coi là dang dở và chuyển tiếp; chỉ có byte ở 0 ms thì tới 200 ms mới chuyển tiếp, 199 ms vẫn giữ.

Trả lời **APPROVED**, hoặc **NOT APPROVED** kèm danh sách lỗi.
