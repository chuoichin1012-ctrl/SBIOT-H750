Câu hỏi trong lúc thực hiện Plan 09b T2: một lỗi ghi đơn lẻ làm mất model Edge. Lỗi này có từ trước 09b, giờ mới lộ ra.

**Hiện tượng.** `test_store`, ở pha `STORE_CUTS` (`write_failures_from(false)`), trên flash mô phỏng: lưu `edgeDevices` trong lúc FlashDB dọn rác (GC), cho đúng một thao tác program trả `NOR_EIO`. Sau khi lưu body thô theo T2, có 5/44 trường hợp sai, so với 0 trước 09b. Lý do: body thô nhỏ hơn nhiều so với văn bản chuẩn hóa trước kia, nên số hiệu thao tác rơi vào chỗ khác.

**Đã sửa ở lớp store (không vá FlashDB).** Thêm `fal_nor_write_errors()`, đếm các lần program hoặc erase bị từ chối. `store_edge_save` so bộ đếm trước khi ghi manifest; nếu có lỗi thì không commit, và đi qua đường mount lại sẵn có. Cách này sửa được 4/5 trường hợp: những lần FlashDB báo thành công dù đã có lần ghi hỏng.

**Còn lại 1 trường hợp (op 2), theo trace:**
1. GC chuyển manifest `edgeDevices` sang sector `0x40000`. op 1 đặt nguồn sang `PRE_DELETE`; op 2 là byte trạng thái của sector `0x40000` (empty → using), và nó thất bại. FlashDB báo "Moving the KV failed, its source is kept".
2. Trong cùng phiên, FlashDB vẫn ghi record mới vào `0x40010` của chính sector đó. Store thấy lỗi FAL nên không commit.
3. Lúc mount lại (hoặc lần boot sau), sector `0x40000` mang trạng thái "empty" nhưng có dữ liệu. Bước khôi phục chuyển manifest `PRE_DELETE` vào `0x40010`, ghi chồng lên record đã có ở đó (NOR chỉ AND được bit), nên bản chép bị hỏng. Sau đó nó xóa nguồn (op 32 `0x0f`). Kết quả: không còn manifest, model quay về `[]`.

Đây là phần còn lại của lỗi 3 trong spec §9.1. Bản vá giữ được nguồn, nhưng FlashDB vẫn dùng một sector mà nó ghi trạng thái không thành công.

**Phạm vi chịu lỗi theo spec §9.1:** phải chịu được mất điện và lỗi đọc thoáng qua; lỗi ghi dai dẳng thì chỉ cần không làm hỏng thêm. Người dùng có quy tắc: không vá thêm FlashDB.

**Các lựa chọn:**
- (a) FAL **ghi lại** program/erase thất bại, tối đa 3 lần, giống cách nó đọc lại. Lỗi ghi thoáng qua thì FlashDB không bao giờ thấy, và vì NOR là AND nên program lại cùng dữ liệu là an toàn. Ma trận sẽ được giữ, nhưng đổi thành lỗi kéo dài 3 lần (luôn hỏng) để vẫn đi qua đường lỗi của FlashDB. Trường hợp op 2 khi đó trở thành "lỗi ghi dai dẳng", và ghi thành giới hạn đã biết.
- (b) Lúc mount, trước FlashDB, store quét các sector "empty" của DB edge; sector nào có dữ liệu thì xóa (chỉ mất record chưa commit). Sửa tận gốc mà không vá FlashDB, nhưng phức tạp hơn.
- (c) Chấp nhận, ghi thành giới hạn đã biết: một lần ghi trạng thái sector thất bại trong GC có thể làm mất model.

Khuyến nghị: **(a)**, giữ cả phần chặn commit khi có lỗi FAL. Xin chọn, hoặc đề xuất cách khác.
