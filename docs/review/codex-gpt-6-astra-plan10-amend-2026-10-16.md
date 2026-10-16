APPROVED

Thiết kế phù hợp và đủ đơn giản; `raw_sendto_if_src` cho phép kiểm từng link độc lập với default route.

Khi thực hiện T4b:
- Chỉ tính Echo Reply đúng link, ID/sequence và trong cửa sổ 2 s; không chờ chặn thread tcpip.
- Kiểm thêm chiều phục hồi: link ưu tiên trả lời lại thì route và DNS quay về đúng link.
- Sửa câu “không phát hiện mất Internet” còn sót trong Global Constraints, cùng spec §7.2.