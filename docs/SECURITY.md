# Security và production provisioning

Firmware development không tự bật Secure Boot, flash encryption hay NVS encryption. Đây là chủ ý: các tính năng production có thể ghi eFuse ở lần boot đầu và không thể hoàn tác bằng cách flash lại firmware.

## Hai profile build

Development dùng `sdkconfig` hiện tại:

```powershell
idf.py -B build-win build
```

Production phải dùng cả build directory và sdkconfig riêng:

```powershell
New-Item -ItemType Directory -Force keys
espsecure generate-signing-key --version 2 keys/callbox_secure_boot_signing_key.pem
idf.py -B build-production `
  -DSDKCONFIG=sdkconfig.production `
  -DSDKCONFIG_DEFAULTS=sdkconfig.production.defaults build
```

Không commit, gửi qua chat hoặc đưa private signing key vào artifact firmware. Thư mục `keys/` đã được Git ignore; key production nên được sao lưu mã hóa và giới hạn quyền truy cập.

Profile production bật và CMake bắt buộc:

- Secure Boot V2 với binary được ký.
- Flash encryption AES-256 ở release mode.
- NVS encryption với key protection bằng HMAC eFuse.
- MQTT có username và password; tùy chọn anonymous không thể bật cùng production.

Nếu thiếu một điều kiện, configure/build dừng bằng lỗi thay vì tạo image production không an toàn.

## Cảnh báo eFuse và flash

Không flash profile production vào board phát triển. Trước provisioning cần xác nhận đúng MAC/serial, lưu signing key, có image recovery hợp lệ và đọc security workflow của đúng phiên bản ESP-IDF/SoC.

Secure Boot V2, flash encryption release và HMAC NVS có thể cấp phát/ghi key cùng các bit bảo vệ vào eFuse. Sau đó thiết bị chỉ chấp nhận image phù hợp; một key sai hoặc bị mất có thể làm thiết bị không thể cập nhật hay khởi động.

ESP-IDF mặc định không đưa bootloader Secure Boot V2 vào lệnh `flash` thông thường. Quy trình manufacturing phải flash bootloader đã ký theo hướng dẫn ESP-IDF, sau đó partition table/application, rồi kiểm tra boot log và eFuse summary trên đúng thiết bị. Không tự động hóa bước ghi eFuse trước khi quy trình này đã được nghiệm thu trên board thử riêng.

## NVS hiện hữu

NVS plaintext cũ không được coi là đã migrate chỉ vì firmware mới bật encryption. Trước lần boot production đầu tiên:

1. Xuất hoặc ghi lại cấu hình cần giữ bằng kênh vận hành tin cậy.
2. Xóa/reprovision partition NVS theo quy trình manufacturing.
3. Boot image production để NVS-HMAC khởi tạo storage mã hóa.
4. Commission lại Wi-Fi, MQTT và mật khẩu portal; xác nhận reboot vẫn đọc được.

Không chạy erase trên thiết bị production đã provision nếu chưa có backup và signing key tương ứng.

## Credential runtime

- Portal trên AP và STA đều dùng username `admin`.
- Mật khẩu mặc định là `Aubot-<MAC6>-9`, với `MAC6` là sáu ký tự hex cuối của factory MAC và cũng là suffix SSID AP. Giá trị legacy `aubot` được đổi tự động rồi persist.
- Mật khẩu portal mới phải dài 12-63 ký tự ASCII in được và có chữ hoa, chữ thường, số, ký tự đặc biệt. Portal không trả lại mật khẩu đang lưu; đổi mật khẩu sẽ hủy phiên hiện tại.
- Sau năm lần đăng nhập sai trong một phút, địa chỉ client bị khóa một phút.
- MQTT không kết nối và portal không lưu cấu hình thiếu username/password. Chỉ broker development cô lập mới được build với `CONFIG_CALLBOX_ALLOW_ANONYMOUS_MQTT=y`.

TCP có xác thực vẫn không mã hóa payload. Dùng TLS khi broker hoặc đường truyền nằm ngoài mạng OT tin cậy, đồng thời triển khai ACL broker giới hạn đúng client ID/topic như [WCS MQTT interface](WCS_MQTT_INTERFACE.md).
