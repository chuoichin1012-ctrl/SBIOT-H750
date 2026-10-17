# Tìm gateway trong LAN và cấu hình Ethernet bằng FinderTool

Ngày 2026-10-17. Người dùng yêu cầu chức năng giống *Windows configuration utility* của USR-M100: tìm thiết bị bằng broadcast, đổi cấu hình mạng, khởi động lại, khôi phục mặc định, mở Web UI. Spec chính: `2026-10-14-stm32-firmware-design.md` (§10.5 trỏ tới tài liệu này).

## 1. Mục tiêu và phạm vi

- **Ai dùng:** kỹ thuật viên lắp đặt và xưởng sản xuất, trên một máy Windows cùng LAN với gateway.
- **Bài toán chính:** gateway có IP không cùng subnet với PC (vừa xuất xưởng ở `192.168.0.7`, hoặc bị đặt sai IP), nên Web UI không mở được. Tool vẫn phải tìm thấy gateway và đổi được IP của nó.
- **Quyết định của người dùng:**
  - tool riêng của SBIOT, giao thức do chúng ta định nghĩa; không tương thích với tool của USR;
  - app Windows có giao diện, viết bằng WinForms;
  - tên app là **FinderTool** (`FinderTool.exe`); dùng logo và màu của SBIOT (logo từ `docs/SBIOT.ai`, nền trong suốt);
  - mọi gói đều gửi broadcast, kể cả lệnh gửi tới gateway đã tìm thấy;
  - **không có tài khoản hay mật khẩu**: gói đúng format quy định (§2.1) là gateway làm. Giữ tool đơn giản.
- **Không làm:**
  - tìm qua router, hay ngoài broadcast domain;
  - cập nhật firmware hay đổi mật khẩu bằng tool, vì Web UI đã làm được;
  - tương thích với tool của USR;
  - nghe trên LTE.

## 2. Giao thức

### 2.1 Gói tin

- UDP, cổng **48600**. Gateway chỉ nghe trên netif Ethernet.
- **Mọi gói, cả của tool lẫn của gateway, đều gửi broadcast** tới `255.255.255.255`. Gateway trả lời về cổng nguồn của gói hỏi.
  - Unicast không dùng được khi PC và gateway khác subnet: PC gửi gói tới IP ngoài subnet của mình cho router, nên gói không tới gateway, dù hai máy cắm chung một switch.
  - Broadcast đi thẳng ra dây, tới mọi máy cùng switch, bất kể IP là gì. Broadcast không qua router.
- Mỗi datagram là một object JSON, tối đa 512 byte. JSON lồng sâu hơn 16 tầng bị cJSON từ chối (`CJSON_NESTING_LIMIT`, Plan 10c).
- Mọi gói đều có:
  - `"sbiot":"disc"` và `"v":1`: chữ ký và phiên bản của giao thức;
  - `"op"`: `search`, `found`, `set`, `restart`, `factory` hoặc `ack`;
  - `"id"`: đúng 8 ký tự hex thường, do tool sinh cho mỗi yêu cầu. Gateway trả lại nguyên văn, để tool ghép đúng câu trả lời với yêu cầu của mình khi có nhiều PC.
- **Gateway kiểm gói trước mọi việc khác.** Gói bị bỏ qua, không trả lời, khi:
  - không phải JSON object, hoặc dài quá 512 byte;
  - thiếu chữ ký, hoặc sai phiên bản;
  - có key trùng nhau ở tầng gốc hay trong `net` (ví dụ hai `mac` hay hai `ip`), vì khi đó không rõ gateway nào hay giá trị nào được chọn;
  - `op` lạ, hoặc là `found`/`ack` (gói của gateway khác);
  - `id` sai dạng;
  - `mac` có mặt nhưng sai dạng. Dạng đúng gồm 17 ký tự `xx:xx:xx:xx:xx:xx`, hex thường.

### 2.2 `search`

PC gửi:
```
{"sbiot":"disc","v":1,"op":"search","id":"7f3a91c2"}
```
Có thể thêm `"mac"` để chỉ hỏi một gateway; khi đó chỉ gateway có đúng MAC trả lời.

Mỗi gateway chờ ngẫu nhiên 0–200 ms (`board_rand32`), để các câu trả lời không dồn vào cùng một lúc, rồi trả lời:
```
{"sbiot":"disc","v":1,"op":"found","id":"7f3a91c2","mac":"02:95:b8:e9:a7:9f","sn":"SB2610000001",
 "model":"SBIOT-H750","fw":"0.7.0","hostname":"sbiot-gw","web":80,"boot":197,
 "eth":{"ip":"192.168.0.7","netmask":"255.255.255.0","gateway":"192.168.0.1"},
 "net":{"ipMode":1,"ip":"192.168.0.7","netmask":"255.255.255.0","gateway":"192.168.0.1","dnsMode":1,
        "dns1":"8.8.8.8","dns2":"1.1.1.1"}}
```
- `eth`: địa chỉ **đang chạy** trên netif Ethernet (với DHCP là địa chỉ được cấp). Tool dùng nó để hiện trong bảng và để mở Web UI.
- `net`: bảy trường Ethernet của config `network` **đã lưu**, đọc từ store mỗi lần trả lời, nên đúng cả khi Web vừa lưu mà chưa restart.
  - Nghĩa giữ như config `network` (spec §7.1): `ipMode` 0 là DHCP, 1 là tĩnh; `dnsMode` 0 là DNS tự động, 1 là thủ công.
  - Tool điền sẵn hộp thoại Network… bằng `net`, không bao giờ bằng DNS đang chạy, vì DNS đang chạy có thể là DNS của LTE.
- `hostname` lấy từ `system.hostname`; `web` là cổng web **đang chạy** (`system.webPort` đọc lúc boot); `model` và `fw` giống `/api/status`.
- `boot` là số lần boot (`store_boot_count`). Tool dùng nó để biết gateway đã restart xong.

### 2.3 Lệnh: `set`, `restart`, `factory`

```
{"sbiot":"disc","v":1,"op":"set","id":"5b20e7d1","mac":"02:95:b8:e9:a7:9f",
 "net":{"ipMode":1,"ip":"10.10.0.7","netmask":"255.255.255.0","gateway":"10.10.0.1","dnsMode":1,
        "dns1":"8.8.8.8","dns2":"1.1.1.1"}}
```
- `restart` và `factory` có cùng các trường, chỉ không có `net`.
- Lệnh **bắt buộc có `mac`**. Lệnh thiếu `mac` thì bị bỏ qua, để một gói gửi nhầm không đổi mọi gateway trong LAN cùng lúc.
- **Gateway xử lý theo thứ tự:**
  1. `mac` không phải của mình: bỏ qua, im lặng.
  2. `set` có `net` sai thì trả `ack` với `"err":"bad"`. `net` sai khi:
     - thiếu trường, hoặc sai kiểu;
     - IPv4 sai cú pháp. Cú pháp đúng: bốn số thập phân 0–255, như spec §7.1;
     - `ipMode` hay `dnsMode` ngoài {0, 1}.

     Gateway chỉ kiểm cú pháp. Tool kiểm phần còn lại (§3), như quan hệ giữa Web UI và gateway (spec §9.1).
  3. **Làm lệnh, rồi trả kết quả thật:**
     - `set`: `store_patch` (§2.4) ghi bảy trường vào config `network`. Thành công thì trả `ack` ok, rồi hẹn restart sau 1 s (`sysmgr_restart_later`). Thất bại thì trả `ack` với `"err":"store"` và không restart;
     - `restart`: trả `ack` ok, rồi hẹn restart sau 1 s;
     - `factory`: gọi `store_factory_reset`, rồi trả `ack` theo kết quả của nó: ok, hoặc `"err":"store"` khi reset chưa xong. Hàm này luôn tự hẹn restart sau 1,5 s, kể cả khi reset chưa xong (`store.c`), nên `ack` vẫn kịp đi trước.
- **Gói `ack`:**
  ```
  {"sbiot":"disc","v":1,"op":"ack","id":"…","mac":"…","ok":true}
  ```
  Khi `ok` là false thì có thêm `err`: `bad` hoặc `store`.

### 2.4 Trên board

- **`components/services/disc_core.c/.h`**: logic thuần, có test host:
  - parse và kiểm gói (§2.1, §2.3);
  - kiểm `net`;
  - dựng gói `found` và `ack`.
- **`components/services/disc.c`**:
  - một task `disc` với socket UDP của lwIP: bind `0.0.0.0:48600`, bật `SO_BROADCAST`, và `SO_BINDTODEVICE` với tên của netif Ethernet. lwIP có sẵn đường này (`udp_bind_netif`): gói nhận chỉ lấy từ Ethernet, và gói gửi đi ra Ethernet trước khi xét route. Vì vậy, khi LTE là route mặc định (`priority` 2), không gói discovery nào đi ra PPP;
  - đọc `system.hostname` và `system.webPort` lúc boot (spec §9.1);
  - lấy địa chỉ Ethernet đang chạy từ `netmgr`. Khi Ethernet **chưa có địa chỉ** (DHCP mà không có server), task vẫn trả lời (người dùng, 2026-10-17):
    - `LWIP_IP_ACCEPT_UDP_PORT` trong `lwipopts.h` cho lwIP nhận gói tới cổng 48600 như cổng DHCP;
    - gói trả lời đi với nguồn `0.0.0.0`;
    - `found` có `eth.ip` là `0.0.0.0`. FinderTool hiện "no address (DHCP)", không mở Web UI, và **Network…** đặt được IP tĩnh cho gateway;
  - gọi `store_patch`, `sysmgr_restart_later` và `store_factory_reset`;
  - được bộ giám sát theo dõi (Plan 11a): timeout 30 s, chờ socket với timeout 1 s;
  - khởi động trước bridge, nên cổng 48600 thuộc về `disc`.
- **`store_patch(name, const cJSON *fields, err, errlen)`**, hàm mới của `store`. Dưới khóa của store, hàm:
  1. đọc JSON đã lưu của config;
  2. thay đúng các member có trong `fields`, giữ nguyên mọi member khác (LTE, `priority`, cả key lạ);
  3. kiểm như `store_set_text`;
  4. ghi.

  Khóa được giữ liên tục từ lúc đọc tới lúc ghi, nên một lần lưu `network` từ Web chen vào giữa không bị mất. Lỗi parse hay hết bộ nhớ thì hàm không ghi gì và trả lỗi.
- **Cổng 48600 dành riêng:**
  - Web UI (`rules.ts`) từ chối 48600 ở mọi cổng local của port1/port2;
  - nếu một config cũ vẫn dùng cổng này, bridge bind thất bại và báo lỗi như mọi lần bind thất bại hiện nay; `disc` vẫn chạy.

## 3. FinderTool (Windows)

- `stm32/tools/finder/`, .NET 10:
  - `Sbiot.Finder.Protocol` (`net10.0`): dựng và đọc gói, broadcast UDP theo từng adapter, ghép câu trả lời theo `id`;
  - `Sbiot.Finder` (`net10.0-windows`, WinForms): giao diện;
  - `Sbiot.Finder.Tests` (xUnit, `net10.0`): chạy được trên Linux/WSL.
- **Phát hành:** `dotnet publish -r win-x64 --self-contained -p:PublishSingleFile=true`, ra một file `FinderTool.exe`, không cần cài đặt.
- **Cửa sổ chính:**
  - danh sách adapter IPv4 của PC. Tool bind socket vào địa chỉ của adapter được chọn rồi mới gửi broadcast, vì PC nhiều card thì broadcast mặc định có thể đi sai card;
  - nút **Search**: gửi `search` rồi chờ 2 s. Bảng hiện MAC, SN, IP (`eth`), hostname, model, firmware; mỗi MAC một dòng, lần trả lời sau thay lần trước;
  - double-click một dòng: mở `http://<eth.ip>:<web>/` bằng trình duyệt mặc định.
- **Các thao tác trên gateway đã chọn:**
  - **Network…**: DHCP/Static, IP, mask, gateway, DNS tự động/thủ công, DNS1, DNS2; điền sẵn từ `net`. Tool kiểm:
    - IPv4 hợp lệ;
    - mask liên tục;
    - IP không phải địa chỉ mạng hay địa chỉ broadcast;
    - gateway cùng subnet với IP, nếu có gateway;
  - **Restart**;
  - **Factory reset**: hỏi xác nhận, và ghi rõ là mọi cấu hình sẽ bị xóa.
- **Mỗi thao tác:**
  1. gửi lệnh rồi chờ `ack` tối đa 10 s, vì việc ghi flash có thể lâu:
     - có `ack`: báo kết quả;
     - không có `ack`: báo **"không rõ kết quả"** và gợi ý bấm Search. Tool không tự gửi lại lệnh;
  2. sau khi lệnh thành công, tool search lại mỗi 3 s, tối đa 120 s (restart có thể chờ modem LTE tắt tới 60 s), cho tới khi MAC đó trả lời với `boot` lớn hơn trước. Khi đó tool cập nhật dòng của gateway. Quá 120 s thì báo "chưa thấy gateway trở lại".

## 4. Kiểm thử

- **Gói mẫu dùng chung:** `stm32/firmware/test/host/disc_vectors.json`, gồm các gói đúng và sai cùng kết quả mong đợi. Cả `test_disc_core` (C) lẫn `Sbiot.Finder.Tests` (C#) đều đọc file này, để hai bên hiểu format giống nhau.
- **Host `test_disc_core`:**
  - kiểm gói: chữ ký, phiên bản, kích thước, kiểu gốc, key trùng ở tầng gốc và trong `net`, `op` lạ, `id` và `mac` sai dạng;
  - lệnh thiếu `mac`, hay `mac` lạ: im lặng;
  - `set` với `net` thiếu trường, sai kiểu, IPv4 sai, hay mode ngoài {0, 1}: `bad`;
  - dựng `found` và `ack`: JSON hợp lệ, có đủ trường.
- **Host `test_store`:**
  - `store_patch` giữ các member khác, kể cả key lạ;
  - lỗi parse thì không ghi gì;
  - khóa: khóa giả của host ghi lại thứ tự lock, đọc, ghi, unlock. Test kiểm rằng `store_patch` đọc và ghi trong **một** lần giữ khóa. Khóa lồng nhau thì được, nhưng độ sâu khóa không về 0 giữa lúc đọc và lúc ghi.
- **C#:** dựng và parse gói khớp các gói mẫu; kiểm cấu hình mạng phía tool; ghép câu trả lời theo `id`.
- **Web UI:** `npm run test:unit` có quy tắc chặn cổng 48600.
- **Bench `disc_check.py` trên board:**
  - `search` thấy board, có đúng MAC và SN của bản ghi liên kết;
  - `set` sang `10.10.0.7/24`; sau restart, search thấy board với `boot` mới và IP mới; rồi `set` về `192.168.0.7`;
  - `set` có `net` sai thì nhận `bad`, và board không restart;
  - `restart`.

  `factory` chỉ test trên host, vì trên board nó đưa `priority` về 1, và LTE sẽ attach lại bằng SIM đang bị khóa.
- **Tool trên Windows của người dùng, với board thật:**
  - Search, Network, Restart, khi PC và gateway khác subnet;
  - PC có nhiều adapter.

  Bài với LTE làm route mặc định (`priority` 2) chờ tới khi SIM dùng được.

## 5. Rủi ro và giới hạn đã biết

- **Không xác thực (người dùng chọn):** mọi máy trong cùng broadcast domain đều có thể đổi IP, restart hay factory reset gateway. Factory reset xóa cả cấu hình MQTT, Edge, chứng chỉ và mật khẩu web. Gói `found` lộ MAC, SN và IP cho mọi máy trong LAN, giống tool của USR. Nếu sau này cần, có thể thêm xác thực ở phiên bản `"v":2`.
- Gateway chỉ kiểm cú pháp. Một cấu hình đúng cú pháp nhưng sai về mạng (ví dụ gateway ngoài subnet) vẫn được lưu; sửa lại bằng tool hoặc bằng Reload.
- Gateway để DHCP trong một LAN không có DHCP server thì không có địa chỉ. Tool vẫn tìm thấy nó (trả lời từ `0.0.0.0`, §2.4) và đặt được IP tĩnh; chỉ có Web UI là không mở được cho tới khi gateway có địa chỉ.
