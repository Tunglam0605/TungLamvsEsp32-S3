/**
 * @file    health_monitor.h
 * @brief   Giám sát nhịp sống các task nghiệp vụ quan trọng của Callbox.
 *
 *          Mỗi task gọi health_monitor_check_in() sau một chu kỳ xử lý hợp
 *          lệ. Lần gọi đầu tiên tự đăng ký task hiện tại; supervisor
 *          chỉ giám sát task đã thực sự chạy, nhờ đó không reset nhầm
 *          trong giai đoạn boot.
 *
 *          Khi một task mất nhịp quá HEALTH_MONITOR_TASK_TIMEOUT_MS:
 *            1. ghi rõ tên task và thời gian bị treo;
 *            2. yêu cầu tất cả DO về OFF;
 *            3. khởi động lại có kiểm soát.
 *
 *          Supervisor cũng đăng ký với ESP-IDF Task Watchdog. Nếu chính
 *          supervisor không còn được lập lịch, TWDT sẽ panic/reset thay
 *          cho đường phục hồi phần mềm nói trên.
 *
 * @note    Không theo dõi buzzer worker vì task đó cố ý chờ queue bằng
 *          portMAX_DELAY. Watchdog chỉ dành cho các task có chu kỳ hữu hạn.
 */
#ifndef CALLBOX_HEALTH_MONITOR_H
#define CALLBOX_HEALTH_MONITOR_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HEALTH_MONITOR_TASK_TIMEOUT_MS 15000U
#define HEALTH_MONITOR_STARTUP_GRACE_MS 30000U
#define HEALTH_MONITOR_STABLE_WINDOW_MS 600000U

typedef enum {
    HEALTH_TASK_IO_HANDLER = 0,
    HEALTH_TASK_STATE_MACHINE,
    HEALTH_TASK_MQTT_SUPERVISOR,
    HEALTH_TASK_MQTT_TX,
    HEALTH_TASK_OUTPUT_RENDERER,
    HEALTH_TASK_NETWORK_STATUS,
    HEALTH_TASK_WIFI_SELECT,
    HEALTH_TASK_COUNT,
} health_task_id_t;

typedef enum {
    /** Full Callbox runtime: monitor every business task which checks in. */
    HEALTH_MONITOR_MODE_NORMAL = 0,
    /** Recovery runtime: only AP/WebUI infrastructure is intentionally active. */
    HEALTH_MONITOR_MODE_RECOVERY,
} health_monitor_mode_t;

/**
 * @brief Inspect reset history, update the RTC failure streak and apply boot backoff.
 *
 * Must be called once, at the very beginning of callbox_app_run(). A cold boot
 * clears stale RTC state. Panic/watchdog resets and controlled restarts keep a
 * bounded failure streak so repeated local faults cannot create a tight reboot
 * loop.
 */
void health_monitor_boot_begin(void);

/** @return true after repeated local failures require AP/WebUI recovery mode. */
bool health_monitor_recovery_requested(void);

/**
 * @brief Khởi động supervisor và cấu hình TWDT 15 giây có panic/reset.
 * @return ESP_OK nếu supervisor sẵn sàng; ESP_ERR_NO_MEM nếu không
 *         tạo được task; hoặc mã lỗi TWDT.
 */
esp_err_t health_monitor_init(health_monitor_mode_t mode);

/**
 * @brief Báo task hiện tại vẫn hoạt động và đã xong một chu kỳ.
 *
 * Lần gọi đầu tiên lưu task handle và bật giám sát slot tương
 * ứng. Hàm không cấp phát heap, không block và an toàn khi gọi
 * trước health_monitor_init().
 */
void health_monitor_check_in(health_task_id_t task_id);

/**
 * @brief Phục hồi có kiểm soát khi một worker phụ phát hiện lỗi
 *        an toàn không thể tự khôi phục trong thời gian hữu hạn.
 *
 * Ghi reason, tắt toàn bộ DO lần cuối rồi restart. Hàm không trả về.
 */
void health_monitor_force_restart(const char *reason) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* CALLBOX_HEALTH_MONITOR_H */
