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
 * Header này CHỈ còn khai báo runtime globals g_config / buzzer_queue.
 */
#ifndef CALLBOX_QUEUES_H
#define CALLBOX_QUEUES_H

#include "callbox_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Tải trọng lệnh buzzer được định nghĩa bởi led_control.h. */
extern QueueHandle_t buzzer_queue;
extern Config_t g_config;

#endif /* CALLBOX_QUEUES_H */
