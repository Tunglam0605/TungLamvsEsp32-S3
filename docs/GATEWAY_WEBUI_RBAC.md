# Gateway WebUI và RBAC

## Kiến trúc

Gateway dùng một HTTP server, một trang đăng nhập `/login`, một cookie phiên
`GWSESSION` và một hệ điều hướng chung. Trang `/` là tổng quan công khai chỉ đọc.
Các workspace được bảo vệ là Factory, Tech, IT và quản trị tài khoản.

Mọi API protected tự kiểm tra session và permission ở server. JavaScript chỉ ẩn
menu theo vai trò để đơn giản UX, không phải hàng rào bảo mật. API chưa đăng nhập
trả `401`, API sai quyền trả `403`; trang protected chưa đăng nhập chuyển `303`
về `/login`.

## Tài khoản mặc định

| Tài khoản | Mật khẩu mặc định | Vai trò |
|---|---|---|
| `admin_factory` | `aubot_factory` | Factory |
| `admin_tech` | `aubot_tech` | Tech |
| `admin_it` | `aubot_it` | IT |
| `admin_aubot` | `admin_aubot` | Super Admin |

Password được lưu trong namespace NVS riêng `gateway_auth` dưới dạng
PBKDF2-HMAC-SHA256 với salt ngẫu nhiên, 12.000 vòng; không lưu hoặc trả plaintext.
Database mặc định chỉ được tạo khi namespace chưa tồn tại. Lỗi đọc/corruption
không tự ghi đè credential. Hàm `gateway_auth_restore_defaults()` là điểm recovery
chủ động dành cho tích hợp factory reset về sau.

## Phiên

Token 32 byte sinh bằng nguồn random của ESP32, biểu diễn thành 64 ký tự hex.
Cookie có `HttpOnly`, `SameSite=Strict`, `Path=/`, `Max-Age=1800`. Firmware giữ tối
đa bốn session đồng thời; session hết hạn sau 30 phút. Logout, disable tài khoản,
đổi hoặc reset password làm mất hiệu lực session liên quan.

## Ma trận quyền

| Chức năng | Public | Factory | Tech | IT | AUBOT |
|---|---:|---:|---:|---:|---:|
| Xem kho và sức khỏe cơ bản | Có | Có | Có | Có | Có |
| Mapping/profile kho | Không | Có | Có | Không | Có |
| Cấu hình Laser chủ động | Không | Có | Có | Không | Có |
| CAN/raw/system diagnostics | Không | Không | Có | Không | Có |
| WiFi/Ethernet/MQTT/SNTP | Không | Không | Không | Có | Có |
| Quản lý tài khoản | Không | Không | Không | Không | Có |

## Route

- Public: `/`, `/login`, `/logo.png`, `/ui.css`, `/ui.js`,
  `/api/public/status`, `/api/auth/me`.
- Common auth: `POST /login`, `POST /logout`, `POST /api/auth/password`.
- Factory: `/app/factory`, `/api/factory/status`, `/api/factory/profile`,
  `/api/factory/warehouse`, `/api/factory/laser/apply`.
- Tech: `/app/tech`, `/api/tech/status`, `/api/laser/config`.
- IT: `/app/it`, `/api/it/config`, `/api/it/wifi-scan`, `/api/it/status`,
  `/api/it/ap`.
- AUBOT: `/app/manage`, `/api/admin/users`, `/api/admin/user/password`,
  `/api/admin/user/enabled`.

Các alias cũ `/debug`, `/cau-hinh`, `/api/debug/*`, `/api/warehouse/*` và
`/api/gateway/*` vẫn tồn tại để tương thích nhưng dùng cùng kiểm tra quyền.
`/dang-nhap` và `/dang-xuat` chỉ chuyển về login thống nhất.

## Dữ liệu công khai và thao tác Laser

`/api/public/status` chỉ trả Gateway ID, profile, health mạng/MQTT/CAN và snapshot
vị trí kho. API này không trả credential, raw CAN, distance, mask hoặc session.

Lưu mapping chỉ ghi cấu hình Warehouse vào NVS. Cấu hình vùng phát hiện chỉ được
gửi xuống cảm biến khi người dùng có quyền bấm “Áp dụng xuống Laser” và xác nhận.
CAN, MQTT `JSON_WAREHOUSE_V1`, profile 8/12 Group và namespace `warehouse_v3`
không thay đổi.

## Giới hạn và hardening tiếp theo

- Chưa ép đổi mật khẩu ở lần đăng nhập đầu; quản trị viên có thể đổi/reset từ UI.
- SameSite Strict giảm CSRF cho deployment local. Nếu WebUI được publish qua một
  reverse proxy hoặc origin không tin cậy, nên bổ sung CSRF token và HTTPS.
- Kiểm thử visual cần browser thật ở desktop/mobile; firmware không dùng CDN,
  webfont hay framework ngoài.
