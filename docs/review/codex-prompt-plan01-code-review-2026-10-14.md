Tiếp tục trong cùng phiên. Plan 01 đã được thực thi theo kiểu Native trên branch `stm32/plan-01-host-core`. Bạn là reviewer duy nhất cho toàn branch. CHỈ đọc: không sửa working tree, index, HEAD hay branch.

## Đã triển khai
- Task 1: tạo cây `stm32/firmware`: cJSON 1.7.19 vendored, `proto` (bỏ RFC 2217), `cfgmodel`, host test và Makefile.
- Task 2: sửa `cfgmodel` theo TDD cho phạm vi STM32: env {2,2,2,2}, bỏ `rfc2217`/`telnet`/`telnetPort`, `MAX_NODES` = 100.
- Task 3: chép UI sang `stm32/web` và sửa (Telnet, RFC 2217, TLS trên mọi cổng, 100 điểm, EC600U, mock STM32), cùng `check_schema.py`/`embed_web.py` đổi đích sang cây `stm32`.

## Plan và spec
- Plan: `stm32/docs/plans/2026-10-14-01-host-core-and-web.md`
- Spec: `stm32/docs/specs/2026-10-14-stm32-firmware-design.md`
- Ledger, gồm các dòng `Ruling:` cần xét: `.superpowers/sdd/2026-10-14-01-host-core-and-web/progress.md`

## Phạm vi git
Base `5ba9501`, head `HEAD`. Diff khoảng 530 KB, phần lớn là code chép nguyên, nên đối chiếu với nguồn ESP32 sẽ nhanh hơn:
- `diff -ru firmware/components/cfgmodel stm32/firmware/components/cfgmodel`
- `diff -ru firmware/components/proto stm32/firmware/components/proto`
- `diff -u firmware/test/host/test_cfgmodel.c stm32/firmware/test/host/test_cfgmodel.c` (tương tự cho `test_proto.c`)
- `diff -ru src stm32/web/src`, cùng `diff -u package.json stm32/web/package.json`
- `diff -u firmware/tools/check_schema.py stm32/firmware/tools/check_schema.py`
- `git show --stat 5ba9501..HEAD`

Bạn có thể chạy các lệnh chỉ đọc hoặc chỉ build trong một thư mục tạm, ví dụ `make -C stm32/firmware/test/host` dùng thư mục `build` đã được ignore, hoặc `python3 stm32/firmware/tools/check_schema.py`.

## Review Focus của plan (kiểm từng mục một cách có chủ đích)
1. Cấu hình cũ có `telnet`/`telnetPort`/`rfc2217` vẫn nạp được bằng `cfg_load`; key bị bỏ biến mất, key hợp lệ được giữ.
2. Các giá trị có được nhờ board có 2 cổng phải được **chấp nhận**: `websocketTarget` = 3, `edgeReport.channel` = 2, thiết bị `uart` = 2, `AI2`/`DI2`.
3. Trang socket của UART2 hiện TLS và upload chứng chỉ với `scope=port2`.
4. Edge đếm theo giới hạn 100.
5. Cây ESP32 không đổi (snapshot sha256).

## Spec là tài liệu tầm nhìn
Với những hành vi spec không nói tới, đánh giá theo kỳ vọng của một người dùng hợp lý.

## Declined to judge
Trước kết luận, liệt kê những hành vi bạn đã cân nhắc rồi gạt ra ngoài phạm vi, mỗi hành vi một dòng kèm lý do.

## Định dạng trả lời (tiếng Việt)
### Strengths
### Issues
#### Critical (Must Fix)
#### Important (Should Fix)
#### Minor (Nice to Have)
Mỗi issue ghi: file:dòng, vấn đề, vì sao quan trọng, cách sửa.
### Declined to judge
### Assessment
**Ready to merge?** Yes | No | With fixes, kèm 1–2 câu lý do.
