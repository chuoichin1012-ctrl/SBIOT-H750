Plan 10, lượt 3: sửa ba trường hợp DNS của lượt 2 (`codex-gpt-6-astra-plan10-r2-2026-10-16.md`), trong ruling 5, Global Constraints và T2.

1. **PPP không cấp DNS:** status callback đọc cặp DNS từ kết quả thương lượng IPCP (`pcb->ipcp_gotoptions.dnsaddr[0..1]`, 0 nếu không có), không bao giờ đọc từ DNS toàn cục. PPP không có DNS thì cặp của cell rỗng.
2. **PPP xuống trong khi route vẫn là Ethernet:** `dns_apply()` chạy sau **mọi** status callback của PPP, cả lúc lên lẫn lúc xuống. Lúc xuống, `cdns()` đã dọn DNS toàn cục xong, nên cặp của link đang chọn được ghi lại. Có test cho trường hợp Ethernet và PPP dùng cùng DNS.
3. **DHCP ACK có ít DNS hơn lần trước:** hook `LWIP_HOOK_DHCP_DNS(netif, n, servers)` được gọi đúng một lần cho mỗi ACK được chấp nhận, kèm cả danh sách (0, 1 hoặc 2 server). ACK không có option DNS thì gọi với n = 0. DHCP không ghi DNS toàn cục. Có test renew 2 → 1 → 0.

`dns_apply()` ghi đủ cả cặp (kể cả 0), sau mỗi lần đổi route, mỗi callback PPP và mỗi DHCP ACK.

**Phạm vi:** `git diff HEAD~1 HEAD -- stm32/docs/plans/2026-10-16-10-lte.md`.

Trả lời APPROVED, hoặc NOT APPROVED kèm điểm cần sửa.
