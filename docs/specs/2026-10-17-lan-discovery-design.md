# Tìm gateway trong LAN và cấu hình Ethernet bằng SBIOT Finder

Ngày 2026-10-17. Người dùng yêu cầu chức năng giống *Windows configuration utility* của USR-M100: tìm thiết bị bằng broadcast, đổi cấu hình mạng, khởi động lại, khôi phục mặc định, mở Web UI. Spec chính: `2026-10-14-stm32-firmware-design.md` (§10.5 trỏ tới tài liệu này).

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
- **Mọi gói, cả của tool lẫn của gateway, đều gửi broadcast** tới `255.255.255.255`. Gateway trả lời về cổng nguồn của gói hỏi. Nhờ vậy, PC và gateway khác subnet vẫn nói chuyện được với nhau. Broadcast này không qua router.
- Mỗi datagram là một object JSON, tối đa 512 byte. JSON lồng sâu hơn 16 tầng bị cJSON từ chối (`CJSON_NESTING_LIMIT`, Plan 10c).
- Mọi gói đều có:
  - `"sbiot":"disc"` và `"v":1`: chữ ký và phiên bản của giao thức;
  - `"op"`: `search`, `found`, `set`, `restart`, `factory` hoặc `ack`;
  - `"id"`: đúng 8 ký tự hex thường do tool sinh cho mỗi yêu cầu. Gateway trả lại nguyên văn, để tool ghép đúng câu trả lời với yêu cầu của mình khi có nhiều PC.
- **Kiểm gói ở gateway, trước mọi việc khác.** Gói bị bỏ qua, không trả lời, khi:
  - không phải JSON object, hoặc dài quá 512 byte;
  - thiếu chữ ký, hoặc sai phiên bản;
  - có key trùng nhau ở tầng gốc hay trong `net`;
  - một trường sai kiểu hay sai dạng. Các dạng hợp lệ:
    - `id`: 8 ký tự hex thường;
    - `mac`: 17 ký tự, dạng `xx:xx:xx:xx:xx:xx`, hex thường;
    - `nonce`: 32 ký tự hex thường;
    - `hmac`: 64 ký tự hex thường;
    - `user`: 1–16 byte, chỉ gồm ký tự ASCII in được (0x20–0x7E).

  Do đó chuỗi đem ký (§2.3) không thể chứa ký tự NUL, dấu `|` lạ hay chuỗi dài quá cỡ. Với các dạng cố định này, dùng dấu phân cách là đủ; không cần chuẩn hóa JSON.

### 2.2 `search` (không xác thực)

PC gửi:
```
{"sbiot":"disc","v":1,"op":"search","id":"7f3a91c2"}
```
Có thể thêm `"mac"` để chỉ hỏi một gateway; khi đó chỉ gateway có đúng MAC trả lời.

Mỗi gateway chờ ngẫu nhiên 0–200 ms, rồi trả lời:
```
{"sbiot":"disc","v":1,"op":"found","id":"7f3a91c2","mac":"02:95:b8:e9:a7:9f","sn":"SB2610000001",
 "model":"SBIOT-H750","fw":"0.7.0","hostname":"sbiot-gw","web":80,"boot":197,
 "eth":{"ip":"192.168.0.7","netmask":"255.255.255.0","gateway":"192.168.0.1"},
 "net":{"ipMode":1,"ip":"192.168.0.7","netmask":"255.255.255.0","gateway":"192.168.0.1","dnsMode":1,
        "dns1":"8.8.8.8","dns2":"1.1.1.1"},
 "nonce":"9c1e…(32 hex)"}
```
- `eth`: địa chỉ **đang chạy** trên netif Ethernet (với DHCP là địa chỉ được cấp). Tool dùng nó để mở Web UI và để hiện trong bảng.
- `net`: bảy trường Ethernet của config `network` **đã lưu**, đọc từ store mỗi lần trả lời. Vì vậy chúng đúng cả khi Web vừa lưu mà chưa restart. Nghĩa giữ như config `network` (spec §7.1): `ipMode` 0 là DHCP, 1 là tĩnh; `dnsMode` 0 là DNS tự động, 1 là thủ công. Tool điền sẵn hộp thoại Network… bằng `net`, không bao giờ bằng DNS đang chạy, vì DNS đang chạy có thể là DNS của LTE.
- `hostname` lấy từ `system.hostname`. `web` là cổng web **đang chạy**, tức `system.webPort` đọc lúc boot. `model` và `fw` giống `/api/status`. `boot` là số lần boot (`store_boot_count`); tool dùng nó để biết gateway đã restart xong.
- `nonce`: §2.4. Không có nonce hợp lệ (RNG hỏng) thì gói `found` không có trường `nonce`.
- Gói `found` không có xác thực. Tool coi nội dung của nó là thông tin để hiện, không phải bằng chứng về danh tính của thiết bị.

### 2.3 Lệnh có xác thực: `set`, `restart`, `factory`

```
{"sbiot":"disc","v":1,"op":"set","id":"5b20e7d1","mac":"02:95:b8:e9:a7:9f","user":"admin","nonce":"9c1e…",
 "net":{"ipMode":1,"ip":"10.10.0.7","netmask":"255.255.255.0","gateway":"10.10.0.1","dnsMode":1,
        "dns1":"8.8.8.8","dns2":"1.1.1.1"},
 "hmac":"…(64 hex)"}
```
- `restart` và `factory` có cùng các trường, chỉ không có `net`.
- `hmac` = HMAC-SHA256 (RFC 2104) với khóa là mật khẩu admin (`system.password`, các byte UTF-8 của nó), viết thành 64 ký tự hex thường. Chuỗi đem ký, giống nhau từng byte ở C và C#:
  ```
  SBIOT-DISC1|<user>|<nonce>|<id>|<op>|<mac>|<tham số>
  ```
  - với `set`, `tham số` là `ipMode,ip,netmask,gateway,dnsMode,dns1,dns2` theo đúng thứ tự: số viết thập phân, chuỗi viết nguyên văn;
  - với `restart` và `factory`, `tham số` rỗng; chuỗi vẫn kết thúc bằng dấu `|`.
- **Gateway xử lý theo thứ tự:**
  1. `mac` không phải của mình: bỏ qua, im lặng.
  2. Đang trong thời gian khóa: bỏ qua, im lặng.
  3. Chưa có nonce hợp lệ (RNG hỏng): `ack` với `"err":"rng"`.
  4. **Xác thực.** Lần sai khi `user` khác `system.username`, `nonce` khác nonce hiện hành hoặc nonce đã hết hạn, hay `hmac` sai. So sánh `hmac` trong thời gian hằng.
     - Lần sai: tăng bộ đếm sai; trả `ack` với `"err":"auth"`. Sai 5 lần liền thì khóa 30 s.
     - Lần đúng: xóa bộ đếm sai, rồi **hủy nonce ngay** (§2.4), trước mọi việc phía sau. Kể cả khi bước 5 hay 6 thất bại, nonce đó không dùng lại được.
  5. `set` có `net` thiếu trường, sai kiểu, IPv4 sai cú pháp, hay `ipMode`/`dnsMode` ngoài {0, 1}: `ack` với `"err":"bad"`. Gateway chỉ kiểm cú pháp. Tool kiểm phần còn lại (§3), như quan hệ giữa Web UI và gateway (spec §9.1).
  6. **Làm lệnh, rồi trả kết quả thật:**
     - `set`: `store_patch` (§2.5) ghi bảy trường vào config `network`. Thành công thì `ack` ok, rồi hẹn restart sau 1 s (`sysmgr_restart_later`). Thất bại thì `ack` với `"err":"store"`, và không restart;
     - `restart`: `ack` ok, rồi hẹn restart sau 1 s;
     - `factory`: gọi `store_factory_reset`, rồi `ack` theo kết quả của nó: ok, hay `"err":"store"` khi reset chưa xong. Hàm này luôn tự hẹn restart sau 1,5 s, kể cả khi reset chưa xong (`store.c`), nên `ack` vẫn kịp đi trước.
- **Gói `ack`:**
  ```
  {"sbiot":"disc","v":1,"op":"ack","id":"…","mac":"…","ok":true,"hmac":"…"}
  ```
  - khi `ok` là false thì có thêm `err`, là một trong `auth`, `bad`, `store`, `rng`;
  - `ack` sau khi xác thực đúng (kết quả ok, `bad` hay `store`) được ký bằng cùng khóa, theo chuỗi:
    ```
    SBIOT-DISC1-ACK|<user>|<nonce của lệnh>|<id>|<op>|<mac>|<ok|err>
    ```
    trong đó phần cuối là `ok` hoặc mã `err`;
  - `ack` của `auth` và `rng` không có `hmac`, vì lúc đó chưa có khóa chung đã kiểm. Tool chỉ coi chúng là gợi ý ("có thể sai mật khẩu");
  - **Tool chỉ tin một kết quả khi `hmac` của `ack` đúng.** Một máy khác trong LAN không giả được "đã lưu" hay "đã xóa".

### 2.4 Nonce

- Gateway giữ **một** nonce hiện hành: 16 byte từ RNG phần cứng (`board_rng`), viết thành 32 ký tự hex thường.
- Nonce được tạo:
  - lúc task `disc` khởi động;
  - khi một gói `search` tới mà không có nonce hiện hành, hoặc nonce đó đã hết hạn.
- Hạn dùng của nonce là 60 s, đo bằng tick của FreeRTOS (đơn điệu). Khởi động lại thì nonce mới, nên lệnh ghi lại từ lần boot trước không còn dùng được.
- Nonce bị hủy khi một lệnh xác thực đúng (bước 4 của §2.3). Hai tool cùng cầm một nonce thì lệnh tới trước thắng; tool kia nhận `auth`, rồi search lại để lấy nonce mới.
- `board_rng` báo lỗi thì không có nonce hiện hành. Buffer viết dở bị bỏ, không có nonce dự phòng (số 0 hay số đoán được). Mọi lệnh có xác thực nhận `ack` với `err:"rng"`, cho tới lần tạo nonce thành công.

### 2.5 Trên board

- **`components/services/disc_core.c/.h`**: logic thuần, có test host:
  - parse và kiểm gói (§2.1);
  - nonce: trạng thái, hạn dùng, hủy; RNG được truyền vào để test thay được;
  - chuỗi ký và HMAC-SHA256 (dùng `sha256_*` sẵn có của `components/ota`); so sánh trong thời gian hằng;
  - bộ đếm sai và thời gian khóa;
  - dựng gói `found` và `ack`.
- **`components/services/disc.c`**:
  - một task `disc`, socket UDP của lwIP bind `0.0.0.0:48600`, bật `SO_BROADCAST` và `SO_BINDTODEVICE` với tên của netif Ethernet. lwIP có sẵn đường này (`udp_bind_netif`): gói nhận chỉ lấy từ Ethernet, gói gửi đi ra Ethernet trước khi xét route. Vì vậy, khi LTE là route mặc định (`priority` 2), không gói discovery nào đi ra PPP;
  - đọc `system.username`, `system.password`, `system.hostname` và `system.webPort` lúc boot (spec §9.1). Đổi mật khẩu trên Web thì tool dùng mật khẩu mới sau lần restart kế tiếp;
  - lấy địa chỉ Ethernet đang chạy từ `netmgr`. Chỉ trả lời khi Ethernet có địa chỉ IPv4;
  - gọi `store_patch`, `sysmgr_restart_later` và `store_factory_reset`;
  - được bộ giám sát theo dõi (Plan 11a): timeout 30 s, chờ socket có timeout 1 s;
  - khởi động trước bridge, nên cổng 48600 thuộc về `disc`.
- **`store_patch(name, const cJSON *fields, err, errlen)`**, hàm mới của `store`. Dưới khóa của store, hàm này:
  1. đọc JSON đã lưu của config;
  2. thay đúng các member có trong `fields`, giữ nguyên mọi member khác, kể cả key lạ;
  3. kiểm như `store_set_text`;
  4. ghi.

  Nhờ giữ khóa từ lúc đọc tới lúc ghi, một lần lưu `network` từ Web chen vào giữa không bị mất. Lỗi parse hay hết bộ nhớ thì không ghi gì, và trả lỗi.
- **Cổng 48600 dành riêng:**
  - Web UI (`rules.ts`) từ chối 48600 ở mọi cổng local của port1/port2;
  - nếu một config cũ vẫn dùng cổng này, bridge bind thất bại, rồi báo lỗi như mọi lần bind thất bại hiện nay; `disc` vẫn chạy.

## 3. SBIOT Finder (Windows)

- `stm32/tools/finder/`, .NET 10, gồm ba project:
  - `Sbiot.Finder.Protocol` (`net10.0`): dựng và đọc gói, chuỗi ký, HMAC (`System.Security.Cryptography.HMACSHA256`), kiểm `hmac` của `ack`, broadcast UDP theo từng adapter, ghép câu trả lời theo `id`;
  - `Sbiot.Finder` (`net10.0-windows`, WinForms): giao diện;
  - `Sbiot.Finder.Tests` (xUnit, `net10.0`): chạy được trên Linux/WSL.
- **Phát hành:** `dotnet publish -r win-x64 --self-contained -p:PublishSingleFile=true`, ra một file `SbiotFinder.exe`, không cần cài đặt.
- **Cửa sổ chính:**
  - danh sách adapter IPv4 của PC. Tool bind socket vào địa chỉ của adapter được chọn, rồi mới gửi broadcast, vì PC nhiều card thì broadcast mặc định có thể đi sai card;
  - nút **Search**: gửi `search`, chờ 2 s. Bảng hiện MAC, SN, IP (`eth`), hostname, model, firmware. Mỗi MAC một dòng; lần trả lời sau thay lần trước;
  - double-click một dòng: mở `http://<eth.ip>:<web>/` bằng trình duyệt mặc định.
- **Các thao tác trên gateway đã chọn:**
  - **Network…**: DHCP/Static, IP, mask, gateway, DNS tự động/thủ công, DNS1, DNS2, điền sẵn từ `net`. Tool kiểm:
    - IPv4 hợp lệ;
    - mask liên tục;
    - IP không phải địa chỉ mạng hay địa chỉ broadcast;
    - gateway cùng subnet với IP, nếu có gateway;
  - **Restart**;
  - **Factory reset**: hỏi xác nhận, và ghi rõ là mọi cấu hình sẽ bị xóa.
- **Mật khẩu:** lần đầu dùng một thao tác, tool hỏi username và password admin. Tool chỉ giữ chúng trong bộ nhớ của phiên, không ghi xuống đĩa.
- **Mỗi thao tác:**
  1. gửi `search` có `mac` để lấy nonce mới. Không có nonce thì báo "gateway không cấp được nonce (RNG)";
  2. gửi lệnh;
  3. chờ `ack` tối đa 10 s, vì việc ghi flash có thể lâu:
     - `ack` có `hmac` đúng: báo kết quả;
     - `auth` (không ký): hỏi lại mật khẩu;
     - không có `ack`, hay `ack` có `hmac` sai: báo **"không rõ kết quả"**, gợi ý bấm Search. Tool không tự gửi lại lệnh;
  4. sau khi `set`, `restart` hay `factory` thành công: search lại mỗi 3 s, tối đa 120 s (restart có thể chờ modem LTE tắt tới 60 s), cho tới khi MAC đó trả lời với `boot` lớn hơn trước. Sau đó cập nhật dòng của nó. Quá 120 s thì báo "chưa thấy gateway trở lại".

## 4. Kiểm thử

- **Vector dùng chung:** `stm32/firmware/test/host/disc_vectors.json`. File gồm khóa, user, nonce, id, op, MAC, tham số, chuỗi đem ký và HMAC mong đợi, cho cả lệnh lẫn `ack`, cùng vài gói mẫu đúng và sai. Cả `test_disc_core` (C) lẫn `Sbiot.Finder.Tests` (C#) đều đọc file này.
- **Host `test_disc_core`:**
  - kiểm gói: chữ ký, phiên bản, kích thước, kiểu gốc, key trùng, dạng của từng trường;
  - MAC lạ;
  - nonce sai, hết hạn, đã hủy; nonce không dùng lại được sau khi restart, vì nonce mới được tạo lúc boot;
  - RNG hỏng: không có nonce, lệnh nhận `rng`, rồi RNG lành lại thì có nonce mới;
  - hai lệnh cùng một nonce: lệnh thứ hai nhận `auth`;
  - user sai, HMAC sai; khóa sau 5 lần sai; mở lại sau 30 s;
  - lệnh xác thực đúng nhưng `net` sai: nhận `bad`, và nonce đã bị hủy;
  - dựng `found` và `ack`: JSON hợp lệ, có đủ trường, `hmac` của `ack` khớp vector.
- **Host `test_store`:** `store_patch` giữ các member khác, kể cả key lạ; lỗi parse thì không ghi; một lần `store_set_text` xen giữa không bị mất (test có hook giữa bước đọc và bước ghi).
- **C#:** chuỗi ký và HMAC khớp vector; parse `found` và `ack`; từ chối `ack` có `hmac` sai; kiểm cấu hình mạng phía tool; ghép câu trả lời theo `id`.
- **Web UI:** `npm run test:unit` có quy tắc chặn cổng 48600.
- **Bench `disc_check.py`, trên board:**
  - `search` thấy board, có đúng MAC và SN của bản ghi liên kết;
  - `set` sang `10.10.0.7/24`; sau restart, search thấy với `boot` mới và IP mới; rồi `set` về `192.168.0.7`;
  - mật khẩu sai trả `auth`; 5 lần sai thì board im 30 s;
  - gửi lại một gói `set` đã dùng thì nhận `auth`;
  - `restart`.

  `factory` chỉ test trên host, vì trên board nó đưa `priority` về 1, và LTE lại attach bằng SIM đang bị khóa.
- **Tool trên Windows của người dùng, với board thật:**
  - Search, Network, Restart, khi PC và gateway khác subnet;
  - PC có nhiều adapter.

  Bài với LTE làm route mặc định (`priority` 2) chờ tới khi SIM dùng được.

## 5. Rủi ro và giới hạn đã biết

- Trong cùng broadcast domain, ai biết mật khẩu admin thì đổi được IP, giống như với Web UI.
- HMAC giấu mật khẩu, chống phát lại lệnh và chống giả `ack`; nó không mã hóa nội dung.
- Gói `found` lộ MAC, SN và IP cho mọi máy trong LAN, giống tool của USR.
- Ai bắt được một lệnh có thể đoán mật khẩu offline; khóa sau 5 lần sai không ngăn được việc này. Mật khẩu admin mạnh là cách phòng duy nhất.
- Một máy trong LAN có thể cố tình gửi lệnh sai để khóa tool 30 s. Đây là giá của thiết kế đơn giản đã chọn.
- Gateway chỉ kiểm cú pháp. Một cấu hình đúng cú pháp nhưng sai về mạng (ví dụ gateway ngoài subnet) vẫn được lưu. Sửa lại bằng tool hoặc bằng Reload.
- Gateway để DHCP trong một LAN không có DHCP server thì không có địa chỉ, nên không trả lời tool. Cách sửa: Reload (về cấu hình xuất xưởng, IP tĩnh `192.168.0.7`), hoặc nối vào mạng có DHCP.
