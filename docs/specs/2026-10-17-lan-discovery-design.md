# Tìm gateway trong LAN và cấu hình Ethernet bằng SBIOT Finder

Ngày 2026-10-17. Người dùng yêu cầu chức năng giống *Windows configuration utility* của USR-M100: tìm thiết bị bằng broadcast, đổi cấu hình mạng, khởi động lại, khôi phục mặc định, mở Web UI. Spec chính: `2026-10-14-stm32-firmware-design.md` (§10 trỏ tới tài liệu này).

## 1. Mục tiêu và phạm vi

- **Ai dùng:** kỹ thuật viên lắp đặt và xưởng sản xuất, trên một máy Windows cùng LAN với gateway.
- **Bài toán chính:** gateway có IP không cùng subnet với PC (vừa xuất xưởng ở `192.168.0.7`, hoặc bị đặt sai IP), nên Web UI không mở được. Tool vẫn phải tìm thấy nó và đổi được IP.
- **Quyết định của người dùng:**
  - tool riêng của SBIOT, giao thức do chúng ta định nghĩa; không tương thích với tool của USR;
  - app Windows có giao diện, viết bằng WinForms;
  - lệnh thay đổi được bảo vệ bằng mật khẩu admin của Web UI, gửi dưới dạng HMAC, không gửi mật khẩu thẳng;
  - mọi gói theo quy ước riêng (§2.1).
- **Không làm:**
  - tìm qua router, hay ngoài broadcast domain (tài liệu M100 cũng không bảo đảm điều này);
  - cập nhật firmware hay đổi mật khẩu bằng tool, vì Web UI đã làm được;
  - tương thích với tool của USR;
  - nghe trên LTE.

## 2. Giao thức

### 2.1 Gói tin

- UDP, cổng **48600**. Gateway chỉ nghe trên netif Ethernet.
- Mỗi datagram là một object JSON, tối đa 512 byte. JSON lồng sâu hơn 16 tầng bị cJSON từ chối (`CJSON_NESTING_LIMIT`, Plan 10c).
- Mọi gói đều có:
  - `"sbiot":"disc"` và `"v":1`: chữ ký và phiên bản của giao thức. Gói không có chữ ký, sai phiên bản, không phải JSON object hay quá 512 byte thì bị bỏ qua, không trả lời;
  - `"op"`: `search`, `found`, `set`, `restart`, `factory` hoặc `ack`;
  - `"id"`: chuỗi 8 ký tự hex do tool sinh. Gateway trả lại nguyên văn, để tool ghép đúng câu trả lời với yêu cầu của mình khi có nhiều PC.
- Gateway gửi mọi câu trả lời bằng **broadcast** `255.255.255.255`, tới cổng nguồn của gói hỏi. Nhờ vậy PC khác subnet vẫn nhận được.

### 2.2 `search` (không xác thực)

PC broadcast:
```
{"sbiot":"disc","v":1,"op":"search","id":"7f3a91c2"}
```
Có thể thêm `"mac"` để chỉ hỏi một gateway; khi đó chỉ gateway có đúng MAC trả lời.

Mỗi gateway chờ ngẫu nhiên 0–200 ms, rồi trả lời:
```
{"sbiot":"disc","v":1,"op":"found","id":"7f3a91c2","mac":"02:95:b8:e9:a7:9f","sn":"SB2610000001",
 "model":"SBIOT-H750","fw":"0.7.0","hostname":"sbiot-gw","ipMode":1,"ip":"192.168.0.7",
 "netmask":"255.255.255.0","gateway":"192.168.0.1","dnsMode":1,"dns1":"8.8.8.8","dns2":"1.1.1.1",
 "web":80,"nonce":"9c1e…(32 hex)"}
```
- Địa chỉ trong gói là địa chỉ **đang chạy** (với DHCP là địa chỉ được cấp). `ipMode` (0 DHCP, 1 tĩnh) và `dnsMode` (0 tự động, 1 thủ công) là cấu hình đã lưu, theo đúng nghĩa của config `network` (spec §7.1).
- `hostname` lấy từ `system.hostname`, `web` từ `system.webPort`, `model` và `fw` giống `/api/status`.
- `nonce` gồm 16 byte ngẫu nhiên lấy từ RNG phần cứng (`board_rng`). Gateway chỉ giữ một nonce hiện hành; nonce đổi khi hết hạn (60 s) và sau mỗi lệnh đã xác thực. Mỗi gói `found` mang nonce hiện hành.

### 2.3 Lệnh có xác thực: `set`, `restart`, `factory`

```
{"sbiot":"disc","v":1,"op":"set","id":"…","mac":"02:95:b8:e9:a7:9f","user":"admin","nonce":"9c1e…",
 "net":{"ipMode":1,"ip":"10.10.0.7","netmask":"255.255.255.0","gateway":"10.10.0.1","dnsMode":1,
        "dns1":"8.8.8.8","dns2":"1.1.1.1"},
 "hmac":"…(64 hex)"}
```
- `restart` và `factory` có cùng các trường, chỉ không có `net`.
- **Chuỗi đem ký**, UTF-8, giống nhau từng byte ở C và C#:
  ```
  SBIOT-DISC1|<user>|<nonce>|<op>|<mac>|<tham số>
  ```
  - `mac` viết thường, ngăn bằng dấu `:`;
  - `tham số` với `set` là `ipMode,ip,netmask,gateway,dnsMode,dns1,dns2` theo đúng thứ tự, số viết thập phân, chuỗi viết nguyên văn; với `restart` và `factory` thì rỗng.
- `hmac` = HMAC-SHA256 (RFC 2104) với khóa là mật khẩu admin (`system.password`, dạng UTF-8), viết thành 64 ký tự hex thường.
- **Gateway kiểm theo thứ tự**, mọi thao tác đều có thời gian chạy không phụ thuộc dữ liệu:
  1. `mac` không phải của mình: bỏ qua, im lặng;
  2. đang trong thời gian khóa: bỏ qua;
  3. `user` khác `system.username`, `nonce` khác nonce hiện hành hoặc đã hết hạn, hay `hmac` sai: tính là một lần sai, trả `ack` với `"ok":false,"err":"auth"`. Sai 5 lần liền thì khóa 30 s; lần đúng thì xóa bộ đếm;
  4. `set` có trường thiếu, sai kiểu, IPv4 sai cú pháp, hay `ipMode`/`dnsMode` ngoài {0, 1}: `ack` với `"ok":false,"err":"bad"`. Gateway chỉ kiểm cú pháp; tool kiểm phần còn lại (§3), như quan hệ Web UI và gateway (spec §9);
  5. hợp lệ: đổi nonce, trả `ack` với `"ok":true`, rồi:
     - `set`: ghi bảy trường đó vào config `network` qua `store_set_text`, giữ nguyên các trường còn lại (LTE, priority), rồi restart sau 1 s. Config chỉ đọc lúc boot (spec §9.1);
     - `restart`: restart sau 1 s;
     - `factory`: gọi đúng đường factory reset hiện có (`store_factory_reset`), đường đó tự restart.
- `ack` có dạng `{"sbiot":"disc","v":1,"op":"ack","id":"…","mac":"…","ok":true}`, kèm `err` khi `ok` là false. Lỗi ghi flash trả `"err":"store"`.

### 2.4 Trên board

- `components/services/disc_core.c/.h`, logic thuần, có test host:
  - parse và kiểm gói;
  - nonce: tạo, hạn dùng, dùng một lần;
  - chuỗi ký và HMAC-SHA256 (dùng SHA-256 sẵn có của `components/ota`);
  - so sánh thời gian hằng; bộ đếm sai và thời gian khóa;
  - dựng gói `found` và `ack`.
- `components/services/disc.c`:
  - một task `disc` với socket UDP của lwIP, bind cổng 48600 và gắn với netif Ethernet, có `SO_BROADCAST`. Gói trả lời broadcast phải đi ra Ethernet cả khi LTE là route mặc định; plan chọn cách gắn (`udp_bind_netif` hay tùy chọn tương đương của socket);
  - đọc `system.username`, `system.password` và hostname lúc boot;
  - lấy địa chỉ đang chạy từ `netmgr`; gọi `store_set_text`, `sysmgr_restart_later` và `store_factory_reset`.
- Task `disc` được bộ giám sát theo dõi (Plan 11a): timeout 30 s, chờ socket có timeout 1 s.
- Chỉ trả lời khi Ethernet có địa chỉ IPv4.

## 3. SBIOT Finder (Windows)

- `stm32/tools/finder/`, .NET 10:
  - `Sbiot.Finder.Protocol` (`net10.0`): dựng và đọc gói, chuỗi ký, HMAC (`System.Security.Cryptography.HMACSHA256`), broadcast UDP theo từng adapter, ghép câu trả lời theo `id`;
  - `Sbiot.Finder` (`net10.0-windows`, WinForms): giao diện;
  - `Sbiot.Finder.Tests` (xUnit, `net10.0`): chạy được trên Linux/WSL.
- **Phát hành:** `dotnet publish -r win-x64 --self-contained -p:PublishSingleFile=true`, ra một file `SbiotFinder.exe`, không cần cài đặt.
- **Cửa sổ chính:**
  - danh sách adapter IPv4 của PC. Tool bind socket vào địa chỉ của adapter được chọn rồi gửi broadcast, vì PC nhiều card thì broadcast mặc định có thể đi sai card;
  - nút **Search**: gửi `search` rồi chờ 2 s. Bảng hiện MAC, SN, IP, hostname, model, firmware; mỗi MAC một dòng, lần trả lời sau thay lần trước;
  - double-click một dòng: mở `http://<ip>:<web>/` bằng trình duyệt mặc định.
- **Các thao tác trên gateway đã chọn:**
  - **Network…**: DHCP/Static, IP, mask, gateway, DNS tự động/thủ công, DNS1, DNS2. Tool kiểm IPv4 hợp lệ, mask liên tục, IP không phải địa chỉ mạng hay broadcast, gateway cùng subnet với IP (nếu có);
  - **Restart**;
  - **Factory reset**: hỏi xác nhận, và ghi rõ là mọi cấu hình sẽ bị xóa.
- **Mật khẩu:** lần đầu dùng một thao tác, tool hỏi username và password admin; chỉ nhớ trong bộ nhớ của phiên, không ghi xuống đĩa.
- **Mỗi thao tác:**
  1. `search` có `mac` để lấy nonce mới;
  2. gửi lệnh;
  3. chờ `ack` 3 s.

  Gặp `err:auth` thì hỏi lại mật khẩu; im lặng thì báo "gateway không trả lời (sai mật khẩu quá nhiều lần thì chờ 30 s)". Sau `set` hay `restart` thành công, tool chờ khoảng 15 s rồi tự search lại để hiện IP mới.

## 4. Kiểm thử

- **Vector dùng chung:** file `stm32/firmware/test/host/disc_vectors.json` gồm khóa, user, nonce, op, MAC, tham số, chuỗi đem ký, HMAC mong đợi, cùng vài gói mẫu đúng và sai. Cả `test_disc_core` (C) lẫn `Sbiot.Finder.Tests` (C#) đều đọc file này.
- **Host `test_disc_core`:**
  - chữ ký, phiên bản, kích thước, kiểu gốc;
  - MAC lạ;
  - nonce sai, hết hạn, đã dùng;
  - user sai, HMAC sai, khóa sau 5 lần sai và mở lại sau 30 s;
  - `set` thiếu trường hay IPv4 sai;
  - dựng `found` và `ack` (JSON hợp lệ, có đủ trường).
- **C#:** chuỗi ký và HMAC khớp vector; parse `found` và `ack`; kiểm cấu hình mạng phía tool; ghép câu trả lời theo `id`.
- **Bench `disc_check.py`, trên board:**
  - `search` thấy board, có đúng MAC và SN của bản ghi liên kết;
  - `set` sang `10.10.0.7/24`, sau restart vẫn search thấy với IP mới, rồi `set` về `192.168.0.7`;
  - mật khẩu sai trả `auth`, 5 lần sai thì board im 30 s;
  - gửi lại một gói `set` đã dùng thì bị `auth`;
  - `restart`.

  `factory` chỉ test trên host, vì trên board nó đưa `priority` về 1 và LTE lại attach trên SIM đang bị khóa.
- **Tool:** build `SbiotFinder.exe`, rồi chạy thử trên Windows của người dùng với board thật: Search, Network, Restart.

## 5. Rủi ro và giới hạn đã biết

- Trong cùng broadcast domain, ai biết mật khẩu admin thì đổi được IP. Điều này giống Web UI. HMAC chỉ bảo vệ mật khẩu và chống phát lại gói; nó không mã hóa nội dung. Gói `found` lộ MAC, SN và IP cho mọi máy trong LAN, giống tool của USR.
- Gateway chỉ kiểm cú pháp. Một cấu hình hợp lệ về cú pháp mà sai về mạng (ví dụ gateway ngoài subnet) vẫn được lưu; sửa lại bằng tool hoặc bằng Reload.
- Mật khẩu đổi trên Web chỉ có hiệu lực với tool sau khi restart, vì config đọc lúc boot.
- Gateway để DHCP trong một LAN không có DHCP server thì không có địa chỉ, nên không trả lời tool. Cách sửa: Reload (về cấu hình xuất xưởng, IP tĩnh `192.168.0.7`), hoặc nối vào mạng có DHCP.
