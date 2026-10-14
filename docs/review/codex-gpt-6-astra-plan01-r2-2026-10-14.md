QUYẾT ĐỊNH: APPROVED WITH CONDITIONS

Tại `67db362`, điều kiện 1–4 đạt; điều kiện 5 còn một lỗi: **mock mode không gửi HTTP**, nên không thể kiểm request trong Network tab. `upload()` gọi trực tiếp `mockUpload()`. [Nguồn](/home/linh/projects/SBIOT-Gateway/src/lib/api.ts:108)

Sửa [Step 6](/home/linh/projects/SBIOT-Gateway/stm32/docs/plans/2026-10-14-01-host-core-and-web.md:496): đặt breakpoint tại `mockUpload`, kiểm đối số `url` bằng `./api/cert?scope=port2&kind=ca`.

Không phát hiện lỗi mới khác trong diff. Giữ phương thức **Native**, reviewer độc lập review cuối.