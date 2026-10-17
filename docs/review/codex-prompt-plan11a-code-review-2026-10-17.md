Plan 11a đã làm xong trên branch `plan11a-identity`, từ 48d940c (plan được duyệt ở r3; Task 0b được duyệt ở r7) tới HEAD. Đây là bước review code bắt buộc trước PR. Plan: `stm32/docs/plans/2026-10-17-11a-identity-and-gates.md`. Ledger và các ruling: `.superpowers/sdd/2026-10-17-11a-identity-and-gates/progress.md`.

**Các task:**
- **0, nhịp LED WORK/WDI:**
  - `pace_due`; `board_iwdg_reload` và `board_wdi_toggle` tách riêng;
  - `wdog` chạy mỗi 50 ms, có ba chế độ (`wdog_set_busy` khi nhận upload, `wdog_set_halt` khi app dừng);
  - bootloader kick theo nhịp bằng `boot_kick`;
  - `/api/status` báo `iwdgMaxGapMs` và `wdiMaxGapMs`.
- **0b, task giám sát:**
  - `hb.c` có slot (tên được sao chép, atomic), lý do reset kèm CRC, và `hb_restart_was_ours`;
  - `wdog_hb_add` đăng ký trong critical section; `wdog` kiểm mỗi 1 s. Task quá hạn thì ghi lý do vào `.noinit` (một cache line, clean D-cache + DSB), rồi ngừng kick;
  - `init` lấy lý do ra sau khi store mount, quyết định theo cờ RSR đã chụp từ trước;
  - heartbeat có trong `tcpip` (sys_timeout), `eth_rx`, `web`, `console`, `cmux_rx`, `lte` (ở `wait_events`, `at_cmd`, `at_wait`), `mqtt` (`retry_wait` chia lát 1 s), `mqtt_io`, các task poll của Edge (beat ở mỗi lần đọc qua `edge_dev_t.hb`), `edge_ctl`, `edge_rpt` (ngủ tối đa 10 s mỗi lát), bridge (beat giữa các pha HTTP), serial RX và `syslog` (đổi chờ `portMAX_DELAY` sang chờ 1 s), `io`;
  - lệnh console `I`; gate `.noinit` trong `check_elf.py`;
  - **ruling:** timeout của task poll Edge là 400 s, của bridge là 600 s, vì công việc hợp lệ ở đó có thể kéo dài hơn mức trong plan.
- **1, bản ghi v2 `SBB2`:** 104 B, chữ ký phủ UID, MAC và SN; UID trong bản ghi phải bằng UID của chip; bản ghi v1 cho kết quả `BIND_EOLD`; có `bind_identity`.
- **2:** `provision.py next-sn` (flock, fsync, rename, fsync thư mục; in SN sau cùng); `flash.sh bind` thêm SN (`SBIOT_SN`, hoặc từ bộ đếm).
- **3:** app lấy MAC và SN từ bản ghi (`app_identity`, `sysmgr_set_serial`); thiếu thì dừng; `netcfg_serial` bị bỏ; thêm lệnh console `R` `RESEED`.
- **4:** lệnh console `C` đọc lại cả hai bản factory record; `factory_cal.py`.
- **5:** `audit_keys.py` được chuyển sang: 192 key, 0 key không được đọc.

**Kết quả:**
- **Host:** `test_board` (pace, hb, reason), `test_bind`, `test_provision.py` (cả `next-sn`: 8 tiến trình đồng thời, lỗi ghi), `test_factory_cal.py`, `test_netcfg`, `test_edge_core` đều xanh; `check_elf` đạt.
- **Board:**
  - chuyển đổi: bootloader dừng ở bản ghi v1, không xóa gì; sau bind thì SN là `SB2610000001` và MAC giữ nguyên `02:95:b8:e9:a7:9f`; `R RESEED` làm factory được seed lại;
  - `iwdgMaxGapMs` 50, `wdiMaxGapMs` 500;
  - `supervisor_check`: ca hang thì board reset và log ghi tên `web`; ca long_wait chạy 7 phút không reset;
  - `factory_cal`: cả hai bản giữ đúng hệ số, và sau reset báo `cal 1`;
  - `ota_check` đang chạy. Script đã được sửa: ảnh 300 KB nhỏ hơn app hiện tại nên không còn đệm âm.

**Phạm vi:** `git diff 48d940c HEAD -- stm32`.

Trả lời APPROVED, hoặc NOT APPROVED kèm lỗi Critical/Important (file:dòng, kịch bản lỗi). Người dùng muốn giữ đơn giản.
