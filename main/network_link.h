/**
 * @file    network_link.h
 * @brief   Aggregator mạng cấp application: Wi-Fi STA HOẶC Ethernet W5500.
 *
 *          Đây là tầng composition/application — biết cả Wi-Fi (wifi_init)
 *          và Ethernet board-specific (bsp_eth) để trả lời câu hỏi duy nhất:
 *          "có bất kỳ uplink IP nào không?". Không phải state machine, không
 *          quản lý transport, không điều hướng routing (ESP TCP/IP stack tự
 *          lo) — chỉ aggregate trạng thái LIVE tại thời điểm gọi.
 *
 *          ═══ SEMANTICS ═══
 *          connected = Wi-Fi STA có IP  HOẶC  W5500 Ethernet có IP.
 *          Không yêu cầu cả hai, không ưu tiên bên nào.
 *
 *          ═══ KHÔNG CÓ STATE ═══
 *          Module này không cache, không tạo mutex/task/timer. Nguồn sự thật
 *          vẫn là wifi_init và bsp_eth.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     mqtt_client.c — người dùng chính (MQTT supervisor)
 * @see     state_machine.c — người dùng thứ hai (admission/retry)
 */
#ifndef NETWORK_LINK_H
#define NETWORK_LINK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  True khi có bất kỳ uplink nào: Wi-Fi STA có IP hoặc W5500 có IP.
 *
 * @return true  có mạng để MQTT kết nối
 * @return false cả hai interface đều không có IP
 */
bool network_link_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_LINK_H */
