/**
 * @file    network_link.c
 * @brief   Triển khai aggregator mạng: Wi-Fi STA OR W5500 Ethernet.
 *
 *          Đây là vị trí hợp lệ DUY NHẤT cho cross-boundary composition
 *          giữa Wi-Fi và Ethernet — sau Phase E.3, wifi_init không còn
 *          include bsp_eth.h / biết Ethernet.
 *
 *          ═══ SEMANTICS GIỮ NGUYÊN ═══
 *          Trước E.3, network_is_connected() (trong wifi_init) = Wi-Fi OR
 *          Ethernet. Logic này được chuyển NGUYÊN sang đây — behavior matrix
 *          không đổi (0/0→false, 1/0→true, 0/1→true, 1/1→true).
 *
 *          KHÔNG: cache, mutex, task, timer, enum transport, routing —
 *          chỉ aggregate live state từ hai nguồn sự thật.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     network_link.h — API
 * @see     wifi_init.h — nguồn trạng thái Wi-Fi STA
 * @see     bsp_eth.h — nguồn trạng thái W5500 Ethernet
 */
#include "network_link.h"

#include "bsp_eth.h"
#include "wifi_init.h"

bool network_link_is_connected(void)
{
    /* Kết nối mạng = STA Wi-Fi HOẶC W5500 Ethernet có IP */
    return wifi_is_connected() != 0 || bsp_eth_is_connected();
}
