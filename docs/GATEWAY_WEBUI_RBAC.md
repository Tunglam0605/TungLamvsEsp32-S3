# Gateway WebUI và RBAC

## Kiến trúc

Gateway dùng một HTTP server, trang đăng nhập `/login`, cookie phiên
`GWSESSION` và thanh điều hướng chung. Trang `/` là tổng quan kho công khai chỉ
đọc. Các workspace được bảo vệ gồm Factory, Tech và IT; AUBOT có quyền vào tất
cả workspace.

Mọi API protected tự kiểm tra session và permission tại server. JavaScript chỉ
điều khiển trải nghiệm hiển thị, không phải hàng rào bảo mật. API chưa đăng nhập
trả `401`, API sai quyền trả `403`; trang protected chưa đăng nhập chuyển `303`
về `/login`.

## Tài khoản cố định

| Tài khoản | Mật khẩu cố định | Vai trò |
|---|---|---|
| `admin_factory` | `aubot_factory` | Factory |
| `admin_tech` | `aubot_tech` | Tech |
| `admin_it` | `aubot_it` | IT |
| `admin_aubot` | `admin_aubot` | AUBOT |

Bốn tài khoản hệ thống luôn được bật. WebUI không cung cấp thao tác tạo, xóa,
bật/tắt hoặc đổi mật khẩu. Credential được lưu trong namespace NVS riêng
`gateway_auth` dưới dạng PBKDF2-HMAC-SHA256 với salt ngẫu nhiên, 12.000 vòng;
không lưu hoặc trả plaintext. `gateway_auth_restore_defaults()` là điểm recovery
chủ động để factory reset khôi phục đúng bộ tài khoản cố định.

## Phiên

Token 32 byte sinh bằng nguồn random của ESP32, biểu diễn thành 64 ký tự hex.
Cookie có `HttpOnly`, `SameSite=Strict`, `Path=/`, `Max-Age=1800`. Firmware giữ
tối đa bốn session đồng thời; session hết hạn sau 30 phút. Logout làm mất hiệu
lực phiên hiện tại.

## Ma trận quyền

| Chức năng | Public | Factory | Tech | IT | AUBOT |
|---|---:|---:|---:|---:|---:|
| Xem tổng quan kho và trạng thái cơ bản | Có | Có | Có | Có | Có |
| Mapping/profile vị trí kho | Không | Có | Có | Không | Có |
| Sửa Warehouse ID/Name | Không | Có | Không | Không | Có |
| Cấu hình vùng quan sát/áp dụng xuống Laser | Không | Có | Có | Không | Có |
| CAN/raw/system diagnostics | Không | Không | Có | Không | Có |
| Gateway ID, WiFi và IP WiFi | Không | Có | Có | Có | Có |
| Broker MQTT, company/site, SNTP | Không | Không | Không | Có | Có |
| Chế độ và IP Ethernet | Không | Không | Không | Có | Có |
| Quản lý hoặc đổi tài khoản/mật khẩu | Không | Không | Không | Không | Không |

WebUI chỉ cấu hình `gateway_id`; không có mã kho/tên kho độc lập. Firmware tự tạo
`warehouse_id` bằng chữ thường của `gateway_id` và dùng `gateway_id` làm tên hiển
thị. Vì vậy đổi Mã Gateway cũng đổi AP, MQTT client ID và toàn bộ topic kho.
IT/AUBOT quản lý `company_id` và `site_id` cùng cấu hình MQTT. Chi tiết contract:
[GATEWAY_MQTT_CONTRACT_V1.md](GATEWAY_MQTT_CONTRACT_V1.md).

Thanh trạng thái chung và workspace Kết nối hiển thị riêng ba địa chỉ runtime:
AP cấu hình `192.168.65.204`, WiFi STA và Ethernet. Ethernet debug dùng IP cố
định `192.168.66.204/24`; ở chế độ uplink, IP Ethernet lấy từ DHCP hoặc cấu
hình tĩnh của IT/AUBOT.

## Route chính

- Public: `/`, `/login`, `/logo.png`, `/ui.css`, `/ui.js`,
  `/api/public/status`, `/api/auth/me`.
- Common auth: `POST /login`, `POST /logout`.
- Factory: `/app/factory`, `/api/factory/status`,
  `/api/factory/warehouse`, `/api/factory/laser/apply` và API định danh kho.
- Tech: `/app/tech`, `/api/tech/status`, `/api/laser/config`.
- IT: `/app/it`, `/api/it/config`, `/api/it/wifi-scan`, `/api/it/status`,
  `/api/it/ap`.
- AUBOT: dùng menu chung để vào tất cả workspace; không có màn hình quản lý tài
  khoản riêng.

Các alias cũ `/debug`, `/cau-hinh`, `/api/debug/*`, `/api/warehouse/*` và
`/api/gateway/*` có thể còn tồn tại để tương thích nhưng phải dùng cùng kiểm tra
quyền. `/dang-nhap` và `/dang-xuat` chỉ chuyển về luồng login thống nhất.

## Dữ liệu công khai và thao tác Laser

`/api/public/status` chỉ trả định danh kho/Gateway cần hiển thị, profile, health
mạng/MQTT/CAN và snapshot vị trí kho. API này không trả credential, raw CAN,
distance, mask hoặc session.

Lưu mapping ghi bản nháp cấu hình Warehouse/Laser vào NVS nhưng chưa cho phép
Gateway tự gửi xuống cảm biến. Người có quyền phải bấm “Áp dụng xuống Laser”
thành công một lần để đánh dấu cấu hình đó là đã commissioning.

Sau lần áp dụng đầu tiên, cờ commissioning và toàn bộ cấu hình được giữ trong
NVS. Khi Gateway khởi động lại, chỉ các vị trí đang bật và đã commissioning mới
được nạp trước task CAN; nếu Laser khởi động và hỏi cấu hình thì Gateway tự trả
lời, không cần đăng nhập hay bấm lại. Sửa nhóm, Laser, khoảng cách, vùng quan sát
hoặc trạng thái phát hiện sẽ hủy cờ này và yêu cầu áp dụng lại; sửa tên/mã hiển
thị không hủy.

Nút “Áp dụng xuống Laser” dùng để gửi `0x002` và `0x000` ngay trong lúc setup.
Gateway không tự phát `0x000` theo heartbeat hoặc theo chu kỳ, nhờ đó ESP32
không làm Laser khởi động lại liên tục. Sau khi trả payload cấu hình cho một
Laser đang hỏi, Gateway gửi thêm `0x002` để khôi phục trạng thái phát hiện vật.
Các bản ghi NVS từ firmware cũ không có cờ commissioning được chuyển đổi theo
kiểu an toàn: giữ mapping nhưng yêu cầu bấm Áp dụng đúng một lần sau nâng cấp.
Việc test MQTT/status không được tự gọi active-config Laser.

## Giới hạn và hardening tiếp theo

- SameSite Strict giảm CSRF cho deployment local. Nếu WebUI được publish qua
  reverse proxy hoặc origin không tin cậy, cần bổ sung CSRF token và HTTPS.
- Kiểm thử visual cần browser thật ở desktop/mobile; firmware không dùng CDN,
  webfont hoặc framework ngoài.
