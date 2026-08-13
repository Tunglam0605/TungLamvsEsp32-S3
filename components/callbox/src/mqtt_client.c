/**
 * @file    mqtt_client.c
 * @brief   Bộ điều hợp vận chuyển MQTT cho Callbox.
 *
 * Giao thức ứng dụng vẫn là callbox/<id>/{cmd,event,status}. Module này
 * ủy thác khung MQTT (framing), keep-alive, reconnect và TLS cho ESP-MQTT
 * để cùng một firmware chạy được với broker TCP nội bộ hoặc broker TLS công
 * cộng.
 *
 * Bộ điều hợp là người tiêu thụ CHỈ ĐỌC telemetry: trạng thái mission/comm
 * cho heartbeat đọc từ status store của CallBox (status.Mission / status.CommState)
 * thay vì gọi Mission Manager — MQTT không bao giờ sửa status store.
 */
#include "callbox_mqtt.h"

#include "sdkconfig.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "app_event_queue.h"
#include "network_link.h"
#include "protocol_types.h"
#include "status.h"
#include "wifi_init.h"
#include "time_sync.h"
#include "health_monitor.h"

#include "freertos/semphr.h"
#include "freertos/task.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "MQTT_CLIENT";

/* Ảnh cấu hình riêng của MQTT (lifetime-safe): sao chép có giới hạn các
 * trường mà adapter cần; KHÔNG giữ con trỏ vào Config_t của caller — portal
 * có thể sửa đổi Config_t khi chạy. */
typedef struct {
    char broker[64];
    uint16_t port;
    MqttTransport_t transport;
    char user[32];
    char pass[64];
    char callbox_id[16];
} mqtt_runtime_config_t;

static mqtt_runtime_config_t s_config;

static esp_mqtt_client_handle_t s_client;
static SemaphoreHandle_t s_client_mutex;
static volatile bool s_mqtt_connected;
/* Chỉ true sau khi broker trả SUBACK thành công cho đúng subscribe hiện tại. */
static volatile bool s_mqtt_operational;
/* Generation loại bỏ callback muộn từ client đã destroy/reconfigure. */
static volatile uint32_t s_client_generation;
static volatile bool s_client_started;
static volatile uint32_t s_publish_epoch;
static volatile uint32_t s_command_session_epoch;
static QueueHandle_t s_publish_queue;
static QueueHandle_t s_reconfigure_mailbox;
static bool s_auth_config_error_logged;
static bool s_adapter_initialized;
static int s_pending_subscribe_id = -1;
static volatile uint32_t s_subscribe_deadline_ms;
static volatile bool s_suback_reconnect_pending;
static mqtt_runtime_stats_t s_runtime_stats;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;

#define MQTT_OUTBOX_LIMIT_BYTES 8192U
#define MQTT_SUBACK_TIMEOUT_MS   15000U
#define MQTT_TX_HEALTH_PERIOD_MS 1000U

typedef enum {
    MQTT_MESSAGE_CRITICAL = 0,
    MQTT_MESSAGE_TELEMETRY,
} mqtt_message_class_t;

typedef struct {
    MQTTMsg_t wire;
    mqtt_message_class_t message_class;
    uint32_t epoch;
    uint32_t seq;
} mqtt_queue_item_t;

static uint32_t mqtt_message_seq(const MQTTMsg_t *message)
{
    if (!message || !message->payload[0]) return 0U;
    const char *field = strstr(message->payload, "\"seq\":");
    if (!field) return 0U;
    field += sizeof("\"seq\":") - 1U;
    char *end = NULL;
    const unsigned long value = strtoul(field, &end, 10);
    return end == field || value > UINT32_MAX ? 0U : (uint32_t)value;
}

static void mqtt_stat_increment(uint32_t *counter)
{
    portENTER_CRITICAL(&s_stats_lock);
    (*counter)++;
    portEXIT_CRITICAL(&s_stats_lock);
}

static uint32_t mqtt_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool mqtt_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void mqtt_latch_lifecycle(app_event_type_t type, uint32_t session_epoch);

/* Sao chép giới hạn, luôn kết thúc '\0'. Không lưu con trỏ vào caller. */
static void mqtt_copy_string(char *dst, size_t dst_size, const char *src)
{
    strncpy(dst, src ? src : "", dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void mqtt_copy_runtime_config(const Config_t *config)
{
    if (!config) return;
    mqtt_copy_string(s_config.broker, sizeof(s_config.broker), config->mqtt_broker);
    s_config.port = config->mqtt_port;
    s_config.transport = config->mqtt_transport;
    mqtt_copy_string(s_config.user, sizeof(s_config.user), config->mqtt_user);
    mqtt_copy_string(s_config.pass, sizeof(s_config.pass), config->mqtt_pass);
    mqtt_copy_string(s_config.callbox_id, sizeof(s_config.callbox_id), config->callbox_id);
    s_auth_config_error_logged = false;
}

static void mqtt_make_runtime_config(mqtt_runtime_config_t *runtime,
                                     const Config_t *config)
{
    if (!runtime || !config) return;
    memset(runtime, 0, sizeof(*runtime));
    mqtt_copy_string(runtime->broker, sizeof(runtime->broker), config->mqtt_broker);
    runtime->port = config->mqtt_port;
    runtime->transport = config->mqtt_transport;
    mqtt_copy_string(runtime->user, sizeof(runtime->user), config->mqtt_user);
    mqtt_copy_string(runtime->pass, sizeof(runtime->pass), config->mqtt_pass);
    mqtt_copy_string(runtime->callbox_id, sizeof(runtime->callbox_id),
                     config->callbox_id);
}

static bool mqtt_credentials_allowed(void)
{
#if defined(CONFIG_CALLBOX_ALLOW_ANONYMOUS_MQTT) && CONFIG_CALLBOX_ALLOW_ANONYMOUS_MQTT
    return true;
#else
    return s_config.user[0] != '\0' && s_config.pass[0] != '\0';
#endif
}

static void mqtt_publish_worker(void *pvParameters)
{
    (void)pvParameters;
    mqtt_queue_item_t item;

    health_monitor_check_in(HEALTH_TASK_MQTT_TX);
    for (;;) {
        if (xQueueReceive(s_publish_queue, &item,
                          pdMS_TO_TICKS(MQTT_TX_HEALTH_PERIOD_MS)) != pdTRUE) {
            health_monitor_check_in(HEALTH_TASK_MQTT_TX);
            continue;
        }
        const MQTTMsg_t *const message = &item.wire;

        if (item.epoch != s_publish_epoch) {
            mqtt_stat_increment(&s_runtime_stats.tx_stale_dropped);
            if (item.seq != 0U) mqtt_stat_increment(&s_runtime_stats.tx_inflight_dropped);
            ESP_LOGW(TAG, "MQTT TX dropped stale epoch=%lu topic=%s",
                     (unsigned long)item.epoch, message->topic);
            health_monitor_check_in(HEALTH_TASK_MQTT_TX);
            continue;
        }

        /* Chỉ worker ưu tiên thấp này mới được chạm vào API TX của ESP-MQTT.
         * Nếu broker/outbox bị chặn, các task nút bấm/trạng thái vẫn được
         * lập lịch bình thường. */
        if (!s_client_mutex ||
            xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
            mqtt_stat_increment(&s_runtime_stats.tx_client_busy);
            if (item.seq != 0U) mqtt_stat_increment(&s_runtime_stats.tx_inflight_dropped);
            /* Không tự requeue vô hạn: Mission transaction là owner retry có
             * backoff và cùng seq. Worker chỉ báo drop/backpressure. */
            ESP_LOGW(TAG, "MQTT TX skipped: client busy (%s)", message->topic);
            health_monitor_check_in(HEALTH_TASK_MQTT_TX);
            continue;
        }

        if (s_client && s_mqtt_operational) {
            const int message_id = esp_mqtt_client_enqueue(
                s_client, message->topic, message->payload, 0, message->qos,
                message->retain, true);
            if (message_id < 0) {
                mqtt_stat_increment(&s_runtime_stats.tx_outbox_failed);
                if (item.seq != 0U) mqtt_stat_increment(&s_runtime_stats.tx_inflight_dropped);
                ESP_LOGW(TAG, "MQTT TX enqueue failed for %s (id=%d)",
                         message->topic, message_id);
            }
        } else {
            mqtt_stat_increment(&s_runtime_stats.tx_not_operational);
            if (item.seq != 0U) mqtt_stat_increment(&s_runtime_stats.tx_inflight_dropped);
        }
        xSemaphoreGive(s_client_mutex);
        health_monitor_check_in(HEALTH_TASK_MQTT_TX);
    }
}

/* ESP-MQTT giữ các con trỏ này suốt vòng đời client. Sao chép mọi giá trị
 * hiển thị cho người dùng trước khi mở kết nối để save/reconfigure không thể
 * để tầng vận chuyển tham chiếu tới cấu hình đã bị sửa đổi. */
static char s_uri[160];
static char s_client_id[64];
static char s_username[32];
static char s_password[64];
static char s_status_topic[96];
static char s_event_topic[96];
static char s_cmd_topic[96];

static const char *mqtt_transport_name(MqttTransport_t transport)
{
    return transport == MQTT_TRANSPORT_TLS ? "TLS" : "TCP";
}

/* Chuỗi trạng thái wire của WCS (chữ thường) — đọc từ status store, không
 * phụ thuộc Mission Manager. */
static const char *mqtt_task_state_name(TaskState_t state)
{
    switch (state) {
    case TASK_QUEUED: return "queued";
    case TASK_ASSIGNED: return "assigned";
    case TASK_LOCKED: return "locked";
    case TASK_COMPLETED: return "completed";
    default: return "idle";
    }
}

static const char *mqtt_comm_state_name(comm_state_t state)
{
    switch (state) {
    case COMM_SYNCING: return "syncing";
    case COMM_READY: return "ready";
    default: return "offline";
    }
}

static void mqtt_handle_command(const char *topic, const char *payload)
{
    if (strcmp(topic, s_cmd_topic) != 0) {
        ESP_LOGD(TAG, "Ignoring command on unexpected topic: %s", topic);
        return;
    }

    app_event_t app_event = {
        .type = APP_EVENT_WCS_COMMAND,
        .session_epoch = s_command_session_epoch,
    };
    if (!protocol_parse_command_json(payload, &app_event.command)) {
        ESP_LOGW(TAG, "Ignoring invalid command JSON");
        return;
    }
    if (!app_event_send(&app_event, 0)) {
        mqtt_stat_increment(&s_runtime_stats.command_dropped);
        ESP_LOGW(TAG, "Application event queue full; dropping WCS command");
        const app_event_t resync_event = {
            .type = APP_EVENT_WCS_RESYNC_REQUIRED,
            .session_epoch = s_command_session_epoch,
        };
        if (!app_event_send(&resync_event, 0)) {
            ESP_LOGE(TAG, "Cannot latch WCS resync after command loss");
        }
        return;
    }
    ESP_LOGI(TAG, "Queued WCS cmd=%s task=%d ref_seq=%lu",
             protocol_command_name(app_event.command.type), app_event.command.task,
             (unsigned long)app_event.command.ref_seq);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)base;
    const uint32_t event_generation = (uint32_t)(uintptr_t)handler_args;
    esp_mqtt_event_handle_t event = event_data;
    if (event_generation != s_client_generation || event->client != s_client) {
        ESP_LOGW(TAG, "Ignoring stale MQTT lifecycle callback generation=%lu",
                 (unsigned long)event_generation);
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        s_mqtt_connected = true;
        s_mqtt_operational = false;
        char subscribe_topic[96];
        strncpy(subscribe_topic, s_cmd_topic, sizeof(subscribe_topic) - 1);
        subscribe_topic[sizeof(subscribe_topic) - 1] = '\0';
        const int message_id = esp_mqtt_client_subscribe(event->client, subscribe_topic,
                                                         MQTT_QoS);
        s_pending_subscribe_id = message_id;
        s_subscribe_deadline_ms = mqtt_now_ms() + MQTT_SUBACK_TIMEOUT_MS;
        s_suback_reconnect_pending = false;
        if (message_id < 0) {
            ESP_LOGE(TAG, "Could not enqueue command subscription for %s (id=%d)",
                     subscribe_topic, message_id);
        }
        ESP_LOGI(TAG, "Connected via %s; subscribe %s (id=%d)",
                 mqtt_transport_name(s_config.transport), subscribe_topic, message_id);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = false;
        s_mqtt_operational = false;
        s_pending_subscribe_id = -1;
        s_subscribe_deadline_ms = 0U;
        s_suback_reconnect_pending = false;
        mqtt_latch_lifecycle(APP_EVENT_MQTT_DISCONNECTED,
                             s_command_session_epoch);
        ESP_LOGW(TAG, "Disconnected; ESP-MQTT will retry while network is available");
        break;
    case MQTT_EVENT_SUBSCRIBED: {
        const bool matching = event->msg_id == s_pending_subscribe_id;
        const bool accepted = event->error_handle == NULL ||
                              event->error_handle->error_type != MQTT_ERROR_TYPE_SUBSCRIBE_FAILED;
        if (!matching || !accepted) {
            ESP_LOGE(TAG, "Command SUBACK rejected/mismatched (expected=%d got=%d accepted=%d)",
                     s_pending_subscribe_id, event->msg_id, accepted);
            break;
        }
        s_pending_subscribe_id = -1;
        s_subscribe_deadline_ms = 0U;
        s_suback_reconnect_pending = false;
        uint32_t next_epoch = s_command_session_epoch + 1U;
        if (next_epoch == 0U) next_epoch = 1U;
        s_command_session_epoch = next_epoch;
        s_mqtt_operational = true;
        mqtt_latch_lifecycle(APP_EVENT_MQTT_CONNECTED, next_epoch);
        ESP_LOGI(TAG, "Command subscription accepted (id=%d); MQTT operational",
                 event->msg_id);
        break;
    }
    case MQTT_EVENT_DATA: {
        /* Lệnh Callbox cố ý nhỏ. Không phân tích payload bị phân mảnh như
         * lệnh hoàn chỉnh; payload vượt kích thước bị bỏ qua. */
        const bool invalid_lengths = event->data_len < 0 || event->total_data_len < 0 ||
                                     event->current_data_offset < 0;
        const bool fragmented = !invalid_lengths &&
                                (event->current_data_offset != 0 ||
                                 event->data_len != event->total_data_len);
        const bool oversized = !invalid_lengths &&
                               (event->topic_len >= 160 || event->total_data_len >= 512);
        if (fragmented) mqtt_stat_increment(&s_runtime_stats.command_fragmented);
        if (oversized) mqtt_stat_increment(&s_runtime_stats.command_oversized);
        if (invalid_lengths || fragmented || oversized || !event->topic ||
            event->topic_len <= 0 ||
            (event->data_len > 0 && event->data == NULL)) {
            ESP_LOGW(TAG, "Ignoring fragmented or oversized MQTT command");
            break;
        }
        char topic[160] = {0};
        char payload[512] = {0};
        memcpy(topic, event->topic, (size_t)event->topic_len);
        if (event->data && event->data_len > 0) {
            memcpy(payload, event->data, (size_t)event->data_len);
        }
        payload[event->data_len] = '\0';
        mqtt_handle_command(topic, payload);
        break;
    }
    case MQTT_EVENT_ERROR:
        s_mqtt_connected = false;
        s_mqtt_operational = false;
        s_pending_subscribe_id = -1;
        s_subscribe_deadline_ms = 0U;
        s_suback_reconnect_pending = false;
        mqtt_latch_lifecycle(APP_EVENT_MQTT_DISCONNECTED,
                             s_command_session_epoch);
        if (event->error_handle) {
            ESP_LOGE(TAG, "Connection error type=%d tls_err=%s verify=0x%x socket=%d",
                     event->error_handle->error_type,
                     esp_err_to_name(event->error_handle->esp_tls_last_esp_err),
                     event->error_handle->esp_tls_cert_verify_flags,
                     event->error_handle->esp_transport_sock_errno);
        } else {
            ESP_LOGE(TAG, "MQTT connection error");
        }
        break;
    default:
        break;
    }
}

static void mqtt_destroy_locked(void)
{
    if (!s_client) return;
    (void)esp_mqtt_client_stop(s_client);
    /* stop đã join MQTT task; callback không còn chạy. Invalidate generation
     * trước destroy để mọi event cũ còn chờ dispatch không chạm runtime mới. */
    s_client_generation++;
    (void)esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    s_client_started = false;
    s_mqtt_connected = false;
    s_mqtt_operational = false;
    s_pending_subscribe_id = -1;
    s_subscribe_deadline_ms = 0U;
    s_suback_reconnect_pending = false;
}

esp_err_t mqtt_client_init(const Config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    if (s_adapter_initialized) {
        if (xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        mqtt_copy_runtime_config(config);
        xSemaphoreGive(s_client_mutex);
        return ESP_OK;
    }

    /* Chụp ảnh cấu hình MQTT ngay trước khi dựng runtime: nếu portal đã lưu
     * trước đó (MQTT khởi tạo muộn), snapshot này đã mang cấu hình mới nhất. */
    mqtt_copy_runtime_config(config);

    s_client_mutex = xSemaphoreCreateMutex();
    if (!s_client_mutex) {
        ESP_LOGE(TAG, "Cannot create MQTT synchronization mutex");
        return ESP_ERR_NO_MEM;
    }
    s_publish_queue = xQueueCreate(24, sizeof(mqtt_queue_item_t));
    if (!s_publish_queue) {
        ESP_LOGE(TAG, "Cannot create MQTT TX queue");
        vSemaphoreDelete(s_client_mutex);
        s_client_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_reconfigure_mailbox = xQueueCreate(1, sizeof(mqtt_runtime_config_t));
    if (!s_reconfigure_mailbox) {
        ESP_LOGE(TAG, "Cannot create MQTT reconfigure mailbox");
        vQueueDelete(s_publish_queue);
        s_publish_queue = NULL;
        vSemaphoreDelete(s_client_mutex);
        s_client_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(mqtt_publish_worker, "mqtt_tx", 3072, NULL, 7, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create MQTT TX task");
        vQueueDelete(s_publish_queue);
        s_publish_queue = NULL;
        vQueueDelete(s_reconfigure_mailbox);
        s_reconfigure_mailbox = NULL;
        vSemaphoreDelete(s_client_mutex);
        s_client_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    memset(&s_runtime_stats, 0, sizeof(s_runtime_stats));
    s_adapter_initialized = true;
    ESP_LOGI(TAG, "ESP-MQTT ready: TCP internal and TLS public broker modes");
    return ESP_OK;
}

/* Lifecycle dùng latch riêng; đọc chênh lệch counter để đưa coalescing vào
 * telemetry MQTT mà không thay đổi hợp đồng queue. */
static void mqtt_latch_lifecycle(app_event_type_t type, uint32_t session_epoch)
{
    const app_event_t app_event = {
        .type = type,
        .session_epoch = session_epoch,
    };
    app_event_queue_stats_t before = {0};
    app_event_queue_stats_t after = {0};
    app_event_queue_get_stats(&before);
    if (!app_event_send(&app_event, 0)) {
        ESP_LOGE(TAG, "Cannot latch MQTT lifecycle event type=%d", type);
        return;
    }
    app_event_queue_get_stats(&after);
    const uint32_t coalesced_delta = after.lifecycle_coalesced -
                                     before.lifecycle_coalesced;
    if (coalesced_delta != 0U) {
        portENTER_CRITICAL(&s_stats_lock);
        s_runtime_stats.lifecycle_coalesced += coalesced_delta;
        portEXIT_CRITICAL(&s_stats_lock);
    }
}

void mqtt_client_subscribe_cmd(void)
{
    if (!s_client || !s_mqtt_connected) return;
    s_mqtt_operational = false;
    s_pending_subscribe_id = esp_mqtt_client_subscribe(s_client, s_cmd_topic, MQTT_QoS);
    s_subscribe_deadline_ms = mqtt_now_ms() + MQTT_SUBACK_TIMEOUT_MS;
}

void mqtt_client_connect(void)
{
    if (!s_client_mutex || !network_link_is_connected()) {
        return;
    }
    if (xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "MQTT connect skipped: client lifecycle mutex timed out");
        return;
    }
    if (s_client_started) {
        xSemaphoreGive(s_client_mutex);
        return;
    }
    if (!s_config.broker[0] || s_config.port == 0) {
        xSemaphoreGive(s_client_mutex);
        return;
    }
    if (!mqtt_credentials_allowed()) {
        if (!s_auth_config_error_logged) {
            ESP_LOGE(TAG, "MQTT connection blocked: username and password are required");
            s_auth_config_error_logged = true;
        }
        xSemaphoreGive(s_client_mutex);
        return;
    }
    if (s_config.transport == MQTT_TRANSPORT_TLS && !time_sync_is_valid()) {
        ESP_LOGI(TAG, "TLS selected: waiting for SNTP time synchronization");
        xSemaphoreGive(s_client_mutex);
        return;
    }

    snprintf(s_uri, sizeof(s_uri), "%s://%s:%u",
             s_config.transport == MQTT_TRANSPORT_TLS ? "mqtts" : "mqtt",
             s_config.broker, (unsigned)s_config.port);
    snprintf(s_client_id, sizeof(s_client_id), CALLBOX_DEVICE_NAME_PREFIX "%s",
             s_config.callbox_id);
    snprintf(s_status_topic, sizeof(s_status_topic), MQTT_STATUS_TOPIC, s_config.callbox_id);
    snprintf(s_event_topic, sizeof(s_event_topic), MQTT_EVENT_TOPIC, s_config.callbox_id);
    snprintf(s_cmd_topic, sizeof(s_cmd_topic), MQTT_CMD_TOPIC, s_config.callbox_id);
    strncpy(s_username, s_config.user, sizeof(s_username) - 1);
    s_username[sizeof(s_username) - 1] = '\0';
    strncpy(s_password, s_config.pass, sizeof(s_password) - 1);
    s_password[sizeof(s_password) - 1] = '\0';

    esp_mqtt_client_config_t config = {
        .broker.address.uri = s_uri,
        .credentials.username = s_username[0] ? s_username : NULL,
        .credentials.authentication.password = s_password[0] ? s_password : NULL,
        .credentials.client_id = s_client_id,
        .session.keepalive = 30,
        .session.last_will.topic = s_status_topic,
        .session.last_will.msg = "{\"online\":false}",
        .session.last_will.qos = MQTT_QoS,
        .session.last_will.retain = true,
        .network.timeout_ms = 10000,
        .network.reconnect_timeout_ms = 5000,
        .outbox.limit = MQTT_OUTBOX_LIMIT_BYTES,
    };
    if (s_config.transport == MQTT_TRANSPORT_TLS) {
        /* Bắt buộc xác thực CA công cộng: không bao giờ tắt kiểm tra hostname/CN. */
        config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }

    s_client = esp_mqtt_client_init(&config);
    if (!s_client) {
        ESP_LOGE(TAG, "Could not create ESP-MQTT client for %s", s_uri);
        xSemaphoreGive(s_client_mutex);
        return;
    }
    const uint32_t client_generation = ++s_client_generation;
    (void)esp_mqtt_client_register_event(
        s_client, MQTT_EVENT_ANY, mqtt_event_handler,
        (void *)(uintptr_t)client_generation);
    const esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start MQTT client: %s", esp_err_to_name(err));
        mqtt_destroy_locked();
    } else {
        s_client_started = true;
        ESP_LOGI(TAG, "Connecting %s to %s as %s", mqtt_transport_name(s_config.transport),
                 s_uri, s_client_id);
    }
    xSemaphoreGive(s_client_mutex);
}

esp_err_t mqtt_client_reconfigure(const Config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    if (!s_client_mutex) {
        /* Portal có thể lưu cấu hình TRƯỚC khi mqtt_client_init() chạy (MQTT
         * khởi động muộn, sau AP/portal). Không tự ý dựng client sớm: chỉ cập
         * nhật snapshot — mqtt_client_init() sau này sẽ dùng cấu hình mới nhất. */
        mqtt_copy_runtime_config(config);
        return ESP_OK;
    }
    if (!s_reconfigure_mailbox) return ESP_ERR_INVALID_STATE;

    mqtt_runtime_config_t request;
    mqtt_make_runtime_config(&request, config);
    /* Mailbox sâu một giữ yêu cầu mới nhất. HTTP không chờ stop/destroy, còn
     * mqtt_comm trở thành owner duy nhất của toàn bộ lifecycle client. */
    return xQueueOverwrite(s_reconfigure_mailbox, &request) == pdPASS
               ? ESP_OK : ESP_FAIL;
}

static bool mqtt_publish(const char *topic, const char *payload, int qos, int retain,
                         mqtt_message_class_t message_class)
{
    if (!s_publish_queue || !topic || !payload || !s_mqtt_operational) {
        mqtt_stat_increment(&s_runtime_stats.tx_not_operational);
        return false;
    }

    mqtt_queue_item_t item = {
        .message_class = message_class,
        .epoch = s_publish_epoch,
    };
    strncpy(item.wire.topic, topic, sizeof(item.wire.topic) - 1);
    strncpy(item.wire.payload, payload, sizeof(item.wire.payload) - 1);
    item.wire.qos = qos;
    item.wire.retain = retain;
    item.seq = message_class == MQTT_MESSAGE_CRITICAL
                   ? mqtt_message_seq(&item.wire) : 0U;

    /* Người gọi không bao giờ chờ trên socket, mutex MQTT hay outbox. */
    if (xQueueSend(s_publish_queue, &item, 0) != pdTRUE) {
        mqtt_stat_increment(&s_runtime_stats.tx_queue_dropped);
        ESP_LOGW(TAG, "MQTT TX queue full; dropping %s", topic);
        return false;
    }
    mqtt_stat_increment(&s_runtime_stats.tx_queued);
    return true;
}

static bool mqtt_publish_task_event(const char *type, int task, uint32_t seq, uint32_t timestamp)
{
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"%s\",\"task\":%d,\"seq\":%lu,\"ts\":%lu}",
             type, task, (unsigned long)seq, (unsigned long)timestamp);
    return mqtt_publish(s_event_topic, payload, MQTT_QoS, false,
                        MQTT_MESSAGE_CRITICAL);
}

bool mqtt_publish_call(int task, uint32_t seq, uint32_t timestamp)
{
    return mqtt_publish_task_event("call", task, seq, timestamp);
}

bool mqtt_publish_cancel(int task, uint32_t seq, uint32_t timestamp)
{
    return mqtt_publish_task_event("cancel", task, seq, timestamp);
}

bool mqtt_publish_sync_request(uint32_t seq, uint32_t timestamp)
{
    char payload[256];
    /* sync được phạm vi theo thiết bị: cố ý không chứa trường task. */
    snprintf(payload, sizeof(payload),
             "{\"type\":\"sync_request\",\"seq\":%lu,\"ts\":%lu,\"fw\":\"%s\"}",
             (unsigned long)seq, (unsigned long)timestamp, CALLBOX_FIRMWARE_VERSION);
    return mqtt_publish(s_event_topic, payload, MQTT_QoS, false,
                        MQTT_MESSAGE_CRITICAL);
}

void mqtt_publish_status(void)
{
    char payload[256];
    const uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000);
    wifi_sta_status_t sta = {0};
    callbox_status_t status_snapshot;
    wifi_get_sta_status(&sta);
    status_get_snapshot(&status_snapshot);
    /* Telemetry CHỈ ĐỌC từ status store — Mission Manager là chủ ghi duy nhất. */
    snprintf(payload, sizeof(payload),
             "{\"online\":true,\"comm\":\"%s\",\"task1\":\"%s\",\"task2\":\"%s\","
             "\"rssi\":%d,\"uptime\":%lu,\"time_synced\":%s,\"fw\":\"%s\",\"ts\":%lu}",
             mqtt_comm_state_name(status_snapshot.CommState),
             mqtt_task_state_name(status_snapshot.Mission[0]),
             mqtt_task_state_name(status_snapshot.Mission[1]),
             (int)sta.rssi,
             (unsigned long)uptime_sec,
             time_sync_is_valid() ? "true" : "false",
             CALLBOX_FIRMWARE_VERSION,
             (unsigned long)time(NULL));
    mqtt_publish(s_status_topic, payload, MQTT_QoS, true, MQTT_MESSAGE_TELEMETRY);
}

void mqtt_get_runtime_stats(mqtt_runtime_stats_t *stats)
{
    if (!stats) return;
    portENTER_CRITICAL(&s_stats_lock);
    *stats = s_runtime_stats;
    portEXIT_CRITICAL(&s_stats_lock);
}

void mqtt_event_handler_task(void *pvParameters)
{
    (void)pvParameters;
    /* Mission Manager tiêu thụ app_event_queue trực tiếp. */
    vTaskDelete(NULL);
}

void mqtt_communication_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t last_heartbeat_ms = 0;
    uint32_t last_start_ms = 0;
    mqtt_runtime_config_t pending_config = {0};
    bool reconfigure_pending = false;
    health_monitor_check_in(HEALTH_TASK_MQTT_SUPERVISOR);
    ESP_LOGI(TAG, "MQTT supervisor started (ESP-MQTT auto reconnect enabled)");

    while (true) {
        uint32_t now_ms = mqtt_now_ms();
        mqtt_runtime_config_t newest_config;
        if (xQueueReceive(s_reconfigure_mailbox, &newest_config, 0) == pdTRUE) {
            pending_config = newest_config;
            reconfigure_pending = true;
        }
        if (reconfigure_pending &&
            xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            /* ESP-MQTT stop có thể chờ task transport kết thúc. Chỉ mqtt_comm
             * thực hiện thao tác này; nếu thư viện kẹt, heartbeat task dừng và
             * Health Monitor phục hồi sau giới hạn 15 giây. */
            mqtt_destroy_locked();
            s_publish_epoch++;
            s_config = pending_config;
            s_auth_config_error_logged = false;
            reconfigure_pending = false;
            xSemaphoreGive(s_client_mutex);
            now_ms = mqtt_now_ms();
            last_start_ms = now_ms - 5000U;
            ESP_LOGI(TAG, "Applied queued MQTT configuration; reconnect pending");
        }
        if (network_link_is_connected() && !s_client_started && now_ms - last_start_ms >= 5000U) {
            last_start_ms = now_ms;
            mqtt_client_connect();
        }
        if (s_mqtt_operational &&
            now_ms - last_heartbeat_ms >= HEARTBEAT_INTERVAL_SEC * 1000UL) {
            mqtt_publish_status();
            last_heartbeat_ms = now_ms;
        }
        if (s_mqtt_connected && !s_mqtt_operational &&
            s_subscribe_deadline_ms != 0U &&
            mqtt_time_reached(now_ms, s_subscribe_deadline_ms)) {
            /* Broker nhận CONNECT nhưng không ACK SUBSCRIBE: ép transport rời
             * phiên hiện tại. ESP-MQTT chuyển WAIT_RECONNECT và tự backoff 5 s;
             * Callbox vẫn OFFLINE, không nhận thao tác khi chưa có command path. */
            ESP_LOGE(TAG, "Command SUBACK timeout; forcing controlled reconnect");
            s_subscribe_deadline_ms = 0U;
            s_suback_reconnect_pending = true;
        }
        if (s_suback_reconnect_pending) {
            /* Portal có thể destroy/recreate client cùng lúc. Khóa vòng đời
             * trước khi dereference handle để timeout không chạm client cũ. */
            if (xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                if (s_suback_reconnect_pending && s_client && s_mqtt_connected &&
                    !s_mqtt_operational) {
                    const esp_err_t err = esp_mqtt_client_disconnect(s_client);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Controlled MQTT disconnect failed: %s",
                                 esp_err_to_name(err));
                    }
                }
                s_suback_reconnect_pending = false;
                xSemaphoreGive(s_client_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        health_monitor_check_in(HEALTH_TASK_MQTT_SUPERVISOR);
    }
}

uint8_t mqtt_is_connected(void)
{
    return s_mqtt_operational ? 1U : 0U;
}
