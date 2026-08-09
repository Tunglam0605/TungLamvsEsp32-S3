# platform_wifi

Provider generic ESP-IDF cho STA/AP netif, credentials/connect/disconnect, scan và factual event bridge. Nó không biết CallBox profile selection, Rescue AP, portal, AP identity hay MQTT.

## DHCP/static lifecycle

CallBox chỉ map Config_t thành platform_wifi_sta_network_config_t:

    Portal save → wifi_apply_config → configure_sta_ip
                → platform_wifi_apply_sta_network_config

Provider sở hữu ESP-NETIF mechanics:

| network.dhcp | DHCP state | Behavior |
|---|---|---|
| true | INIT | giữ default ESP-IDF lifecycle, success |
| true | STARTED | idempotent success |
| true | STOPPED | gọi esp_netif_dhcpc_start |
| false | any applicable | validate IPv4, stop DHCP, set IP/netmask/gateway/DNS |

H.2 commit 6493d671 sửa provider restart; H.2.1 commit 88669b8 sửa product mapping để DHCP runtime thực sự tới provider. Source/build validated; hardware static → DHCP runtime vẫn NOT RUN.

