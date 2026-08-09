/**
 * @file queues.h
 * @brief Runtime globals của main (tạm thời — sẽ được chuyển về chủ sở hữu
 *        thích hợp trong phase sau).
 *
 * Các kiểu cấu hình product (Config_t, WifiProfile_t, MqttTransport_t,
 * MAX_WIFI_PROFILES, CALLBOX_DEVICE_NAME_PREFIX) đã tách sang hợp đồng thuần:
 *
 *     components/callbox/include/callbox_config.h
 *
 * Queue lệnh buzzer nghiệp vụ đã chuyển quyền sở hữu sang led_control
 * (component callbox) từ phase F.3 — header này CHỈ còn khai báo g_config.
 */
#ifndef CALLBOX_QUEUES_H
#define CALLBOX_QUEUES_H

#include "callbox_config.h"

extern Config_t g_config;

#endif /* CALLBOX_QUEUES_H */
