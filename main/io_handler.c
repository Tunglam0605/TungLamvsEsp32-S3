/**
 * @file    io_handler.c
 * @brief   Triển khai task đọc nút bấm + phân phối lệnh LED/buzzer.
 *
 *          Luồng hoạt động của task (chu kỳ IO_SAMPLE_PERIOD_MS = 10 ms):
 *
 *          ═══ VÒNG LẶP CHÍNH ═══
 *          ┌─────────────────────────────────────────────────────┐
 *          │ 1) bsp_di_read_all() chụp snapshot đủ 8 đầu vào     │
 *          │ 2) Debounce độc lập từng nút (40 ms ổn định)        │
 *          │ 3) Gói vào io_state_t → xQueueOverwrite(io_queue)   │
 *          │ 4) Drain led_queue → led_control_apply_command()    │
 *          └─────────────────────────────────────────────────────┘
 *
 *          Kỹ thuật debounce:
 *            - Mỗi nút có 3 biến: raw (mức vừa đọc), candidate (mức chờ
 *              xác nhận), stable (mức đã công bố).
 *            - Khi raw đổi khác candidate → reset bộ đếm thời gian.
 *            - Chỉ khi candidate giữ nguyên >= IO_DEBOUNCE_MS mới trở
 *              thành stable (công bố, ghi log nhấn/thả).
 *            - Mẫu đầu tiên sau khởi động được nạp thẳng vào stable để
 *              không sinh sự kiện "nhấn" giả khi nút đang giữ lúc boot.
 *
 * @note    Mức logic do BSP trả về: 1 = nút nhấn (đã đảo active-low).
 *          Không đọc GPIO trực tiếp ở task này.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     io_handler.h — API
 * @see     callbox_io.c — chọn kênh DI theo mapping
 * @see     led_control.c — xử lý lệnh LED/buzzer
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "bsp_di.h"
#include "io_handler.h"
#include "callbox_io.h"
#include "status.h"
#include <time.h>

static const char *TAG = "IO_HANDLER";

/* Chu kỳ lấy mẫu và thời gian chống nhiễu (ngưỡng ổn định) */
#define IO_SAMPLE_PERIOD_MS 10U
#define IO_DEBOUNCE_MS      100U
#define IO_PRESS_GUARD_MS   300U

/* Queue toàn cục nội bộ: dữ liệu nút và lệnh LED giữa các task */
static QueueHandle_t s_io_queue = NULL;
static QueueHandle_t s_button_event_queue = NULL;

/* Mask các nút đang nhấn, đã chống nhiễu — dùng chung cho chẩn đoán */
static volatile uint8_t s_stable_input_mask = 0;

void io_handler_init(void)
{
    /* Tạo hai queue 1 phần tử (đè giá trị cũ) để truyền trạng thái nút */
    s_io_queue = xQueueCreate(1, sizeof(io_state_t));
    s_button_event_queue = xQueueCreate(16, sizeof(ButtonMsg_t));

    if (s_io_queue == NULL || s_button_event_queue == NULL) {
        ESP_LOGE(TAG, "[IO_INIT_ERR] Failed to create queues");
        return;
    }

    /* GPIO đã được cấu hình bởi BSP (bsp_di_init trong bsp_board_init) */
    const callbox_io_mapping_t *m = callbox_io_get_mapping();
    ESP_LOGI(TAG, "[IO_INIT] ✓ I/O handler initialized (buttons active-low via BSP)");
    ESP_LOGI(TAG, "[IO_INIT] Sample=%u ms (%u Hz), debounce=%u ms",
             IO_SAMPLE_PERIOD_MS, 1000U / IO_SAMPLE_PERIOD_MS, IO_DEBOUNCE_MS);
    ESP_LOGI(TAG, "[IO_INIT] - DI%d: BTN_TASK1 (Exchange Cart)", m->btn_task1 + 1);
    ESP_LOGI(TAG, "[IO_INIT] - DI%d: BTN_TASK2 (Supply Empty)", m->btn_task2 + 1);
    ESP_LOGI(TAG, "[IO_INIT] - DI%d: BTN_CANCEL", m->btn_cancel + 1);
}

QueueHandle_t io_handler_get_io_queue(void)
{
    return s_io_queue;
}

QueueHandle_t io_handler_get_button_event_queue(void)
{
    return s_button_event_queue;
}

uint8_t io_handler_get_stable_input_mask(void)
{
    /* Đọc biến volatile — dùng cho io_debug / portal hiển thị */
    return s_stable_input_mask;
}

void io_handler_task(void *pvParameters)
{
    (void)pvParameters;   /* Không dùng tham số (NULL) */
    ESP_LOGI(TAG, "[IO_TASK] I/O handler task started");
    io_state_t io_state = {0};
    uint32_t task_cycle = 0;
    const callbox_io_mapping_t *m = callbox_io_get_mapping();

    /*
     * Debounce từng đầu vào logic một cách độc lập. Mức thô phải không đổi
     * trong IO_DEBOUNCE_MS trước khi trở thành trạng thái được công bố.
     * Mẫu đầu tiên xác lập trạng thái ban đầu và không bao giờ tạo sự kiện
     * nhấn giả sau khi boot.
     */
    uint8_t raw_state[3] = {0};
    uint8_t candidate_state[3] = {0};
    uint8_t stable_state[3] = {0};
    TickType_t candidate_since[3] = {0};
    TickType_t last_press_tick[3] = {0};
    uint8_t debounce_initialized = 0;

    while (1) {
        task_cycle++;
        TickType_t now_ticks = xTaskGetTickCount();

        /* BSP chỉ đọc snapshot 8DI.  Lớp này ánh xạ 3 nút nghiệp vụ
         * và debounce; status.IN[] vì thế luôn là trạng thái vật lý đã
         * địa hóa theo logic active-low của BSP. */
        const uint8_t input_mask = bsp_di_read_all();
        status_set_inputs(input_mask);
        raw_state[0] = (input_mask & (1U << m->btn_task1)) ? 1U : 0U;
        raw_state[1] = (input_mask & (1U << m->btn_task2)) ? 1U : 0U;
        raw_state[2] = (input_mask & (1U << m->btn_cancel)) ? 1U : 0U;

        if (!debounce_initialized) {
            /* Lần đọc đầu: nạp mức hiện tại thẳng vào cả candidate + stable,
             * tránh sinh sự kiện "nhấn" giả cho nút đang giữ khi boot. */
            for (int i = 0; i < 3; i++) {
                candidate_state[i] = raw_state[i];
                stable_state[i] = raw_state[i];
                candidate_since[i] = now_ticks;
            }
            debounce_initialized = 1;
        } else {
            for (int i = 0; i < 3; i++) {
                if (raw_state[i] != candidate_state[i]) {
                    /* Mức mới đổi khác ứng viên: bắt đầu lại bộ đếm debounce */
                    candidate_state[i] = raw_state[i];
                    candidate_since[i] = now_ticks;
                } else if (candidate_state[i] != stable_state[i] &&
                           (now_ticks - candidate_since[i]) >= pdMS_TO_TICKS(IO_DEBOUNCE_MS)) {
                    /* Ứng viên giữ ổn định đủ lâu: công bố thành stable */
                    stable_state[i] = candidate_state[i];
                    const char *button_name = (i == 0) ? "BTN_TASK1" :
                                              (i == 1) ? "BTN_TASK2" : "BTN_CANCEL";
                    ESP_LOGI(TAG, "[IO] %s %s (cycle=%lu)", button_name,
                             stable_state[i] ? "PRESSED" : "RELEASED",
                             (unsigned long)task_cycle);

                    /* Công bố cả hai cạnh cho button gate. Gate giữ latch PRESSED
                     * tới khi RELEASED đến, nên lớp thứ hai — không phải máy
                     * trạng thái task — sở hữu việc chuẩn hóa cạnh. */
                    if (stable_state[i] != 0U) {
                        /* Nhấn thực là cạnh LOW ổn định (BSP đã chuyển
                         * active-low thành logic 1). Tiếp điểm cơ có thể tạo
                         * chu kỳ HIGH/LOW dài hơn cửa sổ debounce; canh (guard)
                         * cạnh được chấp nhận để nhấn chập chờn nhanh không
                         * thể làm tràn hàng đợi ứng dụng. */
                        const TickType_t press_guard = pdMS_TO_TICKS(IO_PRESS_GUARD_MS);
                        if (last_press_tick[i] != 0 &&
                            (now_ticks - last_press_tick[i]) < press_guard) {
                            ESP_LOGD(TAG, "[IO] BTN_%d press ignored (guard %ums)",
                                     i + 1, IO_PRESS_GUARD_MS);
                            continue;
                        }
                        last_press_tick[i] = now_ticks;
                    }

                    ButtonMsg_t event = {
                        .button_id = i + 1,
                        .state = stable_state[i] ? BTN_PRESSED : BTN_RELEASED,
                        .timestamp = (uint32_t)time(NULL),
                    };
                    if (xQueueSend(s_button_event_queue, &event, 0) != pdTRUE) {
                        ESP_LOGW(TAG, "[IO] Button event queue full; dropping BTN_%d", i + 1);
                    }
                }
            }
        }

        /* Góng trạng thái logic vào io_state và tính mask tổng */
        io_state.btn_1 = stable_state[0];
        io_state.btn_2 = stable_state[1];
        io_state.btn_3 = stable_state[2];
        s_stable_input_mask = (uint8_t)((stable_state[0] ? (1U << m->btn_task1) : 0U) |
                                        (stable_state[1] ? (1U << m->btn_task2) : 0U) |
                                        (stable_state[2] ? (1U << m->btn_cancel) : 0U));

        /* Gửi trạng thái I/O cho máy trạng thái (xQueueOverwrite: luôn giữ mới nhất) */
        xQueueOverwrite(s_io_queue, &io_state);

        vTaskDelay(pdMS_TO_TICKS(IO_SAMPLE_PERIOD_MS));
    }
}
