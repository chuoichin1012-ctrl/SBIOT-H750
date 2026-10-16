APPROVED

Hai lỗi vòng 1 đã được xử lý đúng; không thấy lỗi Critical/Important mới. `test_bridge_core` chạy đạt; không chạy lại loopback/bench trong sandbox.

Ghi chú nhỏ: `test_mutual_oom` nên kiểm `s.peer_cn` sau `end()`/`pthread_join` để tránh báo lỗi giả khi thread server chưa ghi xong CN.