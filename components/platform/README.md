# Platform providers

Platform bọc dịch vụ ESP-IDF thành API ổn định cho product code. Nó không biết CallBox ID, profile selection, Rescue AP, Mission hay GPIO board.

| Provider | Responsibility |
|---|---|
| [platform_wifi](platform_wifi/README.md) | Wi-Fi/netif/scan/AP-STA factual mechanics |
| [platform_nvs](platform_nvs/README.md) | persistence primitive |
| [platform_time](platform_time/README.md) | SNTP/time primitive |

CallBox owns policy; Platform owns raw ESP-IDF lifecycle.

