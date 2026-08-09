# platform_wifi

Generic ESP-IDF Wi-Fi/netif provider. Module này sở hữu raw esp_wifi, esp_netif và event handler mechanics: STA/AP netif, factual STA/AP state, credentials/connect/disconnect, scan và provider-neutral event bridge. Nó không biết profile selection, Rescue AP, portal policy hoặc CallBox identity.

Public API dùng platform_wifi types, không expose raw ESP-IDF Wi-Fi structs. Scan caller cấp buffer; auth mode được map về enum platform stable.

## DHCP / static provider behavior

- dhcp=true + DHCP INIT: giữ default STA-connected lifecycle của ESP-IDF.
- dhcp=true + DHCP STARTED: idempotent, không restart.
- dhcp=true + DHCP STOPPED: runtime static→DHCP gọi esp_netif_dhcpc_start; lỗi provider được trả về.
- dhcp=false: validate IPv4, stop DHCP, set IP/netmask/gateway/DNS.

Không force disconnect STA và không tự quản lý policy product. Product Wi-Fi policy ở CallBox wifi_init.
