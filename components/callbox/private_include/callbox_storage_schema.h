/**
 * @file    callbox_storage_schema.h
 * @brief   PRODUCT INTERNAL — hằng số schema persistence của CallBox.
 *
 *          Header này CHỈ dành cho persistence implementation
 *          (callbox_config_store / sequence_store). Application/business
 *          modules KHÔNG được include trực tiếp.
 *
 *          ═══ BẤT BIẾN ═══
 *          Namespace và key ở đây là hợp đồng NVS với firmware cũ — tuyệt đối
 *          KHÔNG rename, đổi namespace, đổi type, thêm version prefix hay
 *          migrate blob. Firmware mới phải đọc nguyên NVS của firmware cũ
 *          mà không cần factory reset.
 *
 *          Platform (components/platform/platform_nvs) KHÔNG biết schema này.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 */
#ifndef CALLBOX_STORAGE_SCHEMA_H
#define CALLBOX_STORAGE_SCHEMA_H

/* Namespace duy nhất cho mọi persistence của CallBox. */
#define CALLBOX_STORAGE_NAMESPACE "callbox"

#define CALLBOX_STORAGE_CFG_PARTITION "nvs_cfg"
#define CALLBOX_STORAGE_RUNTIME_PARTITION "nvs_runtime"

/* Key sequence (u32) — sequence_store. */
#define CALLBOX_STORAGE_SEQ_KEY "seq_num"

/* Key Wi-Fi chính + mạng đang dùng. */
#define CALLBOX_STORAGE_WIFI_SSID_KEY "wifi_ssid"
#define CALLBOX_STORAGE_WIFI_PASS_KEY "wifi_pass"

/* Key tĩnh IP (có chủ đích để trống khi DHCP). */
#define CALLBOX_STORAGE_WIFI_DHCP_KEY "wifi_dhcp"
#define CALLBOX_STORAGE_WIFI_IP_KEY "wifi_ip"
#define CALLBOX_STORAGE_WIFI_NETMASK_KEY "wifi_mask"
#define CALLBOX_STORAGE_WIFI_GATEWAY_KEY "wifi_gw"
#define CALLBOX_STORAGE_WIFI_DNS_KEY "wifi_dns"

/* Key MQTT. */
#define CALLBOX_STORAGE_MQTT_BROKER_KEY "mqtt_broker"
#define CALLBOX_STORAGE_MQTT_PORT_KEY "mqtt_port"
#define CALLBOX_STORAGE_MQTT_TRANSPORT_KEY "mqtt_tls"
#define CALLBOX_STORAGE_MQTT_USER_KEY "mqtt_user"
#define CALLBOX_STORAGE_MQTT_PASS_KEY "mqtt_pass"

/* Key CallBox ID + mật khẩu portal. */
#define CALLBOX_STORAGE_CALLBOX_ID_KEY "callbox_id"
#define CALLBOX_STORAGE_WEB_PASS_KEY "web_pass"

/* Key SNTP. */
#define CALLBOX_STORAGE_SNTP_PRIMARY_KEY "sntp_primary"
#define CALLBOX_STORAGE_SNTP_FALLBACK_KEY "sntp_fallback"

/* Key danh sách profile Wi-Fi (dynamic: wifi{i}_ssid / wifi{i}_pass). */
#define CALLBOX_STORAGE_WIFI_COUNT_KEY "wifi_count"
#define CALLBOX_STORAGE_WIFI_PROFILE_KEY_FMT "wifi%u_%s"
#define CALLBOX_STORAGE_WIFI_PROFILE_FIELD_SSID "ssid"
#define CALLBOX_STORAGE_WIFI_PROFILE_FIELD_PASS "pass"

#endif /* CALLBOX_STORAGE_SCHEMA_H */
