/**
 * @file bsp_can.c
 * @brief Trien khai giao dien CAN Classic cach ly cap board.
 *
 * Luong RX: TWAI ISR -> can_rx_done_callback() -> s_rx_queue -> Task.
 * Callback chi copy frame hop le vao queue, khong log va khong phan tich
 * protocol de giu thoi gian ISR ngan. Task consumer tu xu ly ID/payload.
 *
 * Luong TX: Task -> bsp_can_send() -> hang doi TX cua TWAI -> transceiver.
 * ESP_OK cua send chi nghia driver da nhan frame; BSP khong so huu ACK,
 * retry nghiep vu hay fail-safe cua ung dung.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 * @see     bsp_can.h - public API va gioi han trach nhiem BSP
 */
#include "bsp_can.h"

#include <string.h>

#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define BSP_CAN_TX_GPIO          GPIO_NUM_2
#define BSP_CAN_RX_GPIO          GPIO_NUM_3
#define BSP_CAN_RX_QUEUE_DEPTH   32U
#define BSP_CAN_TX_QUEUE_DEPTH   16U

static const char *TAG = "BSP_CAN";

/* s_node la owner duy nhat cua TWAI controller. s_rx_queue la cau noi ISR
 * sang Task; khong expose QueueHandle ra ngoai de BSP giu ownership. */
static twai_node_handle_t s_node;
static QueueHandle_t s_rx_queue;
/* ESP-IDF TWAI v6 queues pointers to twai_frame_t rather than copying the
 * frame. Keep a caller's stack frame alive until the controller is idle, and
 * serialize senders so no queued pointer can outlive this function. */
static SemaphoreHandle_t s_tx_mutex;
/* Tang trong ISR khi RX nhanh hon Task consumer; chi dung de chan doan. */
static volatile uint32_t s_rx_queue_overflow_count;
static volatile uint32_t s_tx_success_count;
static volatile uint32_t s_tx_failed_count;
static volatile uint32_t s_rx_callback_count;
static volatile uint32_t s_rx_enqueued_count;
static volatile uint32_t s_rx_read_failed_count;
static volatile uint32_t s_rx_rejected_count;
static volatile uint32_t s_rx_rejected_ide_count;
static volatile uint32_t s_rx_rejected_rtr_count;
static volatile uint32_t s_rx_rejected_fdf_count;
static volatile uint32_t s_rx_rejected_id_count;
static volatile uint32_t s_rx_rejected_dlc_count;
static volatile uint32_t s_last_rejected_id;
static volatile uint8_t s_last_rejected_dlc;
static volatile uint8_t s_last_rejected_flags;
static volatile uint32_t s_error_callback_count;
static volatile uint32_t s_arbitration_lost_count;
static volatile uint32_t s_bit_error_count;
static volatile uint32_t s_form_error_count;
static volatile uint32_t s_stuff_error_count;
static volatile uint32_t s_ack_error_count;
static volatile uint32_t s_last_error_flags;

/* Callback chay trong ISR context. Khong duoc dung API block, malloc, log hay
 * decode protocol o day. Driver yeu cau twai_node_receive_from_isr() duoc goi
 * ngay trong callback de lay frame vua nhan tu hardware FIFO. */
static IRAM_ATTR bool can_rx_done_callback(twai_node_handle_t handle,
                                           const twai_rx_done_event_data_t *event,
                                           void *user_context)
{
    (void)event;
    (void)user_context;

    ++s_rx_callback_count;
    /* Buffer stack co kich thuoc Classic CAN toi da 8 byte. */
    uint8_t data[BSP_CAN_MAX_DLC] = { 0 };
    twai_frame_t rx = {
        .buffer = data,
        .buffer_len = sizeof(data),
    };
    /* BSP chi chuyen Standard Classic Data frame. Extended/RTR/FD duoc loai
     * ngay tai bien hardware de application khong nham protocol. */
    if (twai_node_receive_from_isr(handle, &rx) != ESP_OK) {
        ++s_rx_read_failed_count;
        return false;
    }
    if (rx.header.ide || rx.header.fdf || rx.header.id > BSP_CAN_STANDARD_ID_MAX ||
        rx.header.dlc > BSP_CAN_MAX_DLC) {
        ++s_rx_rejected_count;
        s_rx_rejected_ide_count += rx.header.ide ? 1U : 0U;
        s_rx_rejected_fdf_count += rx.header.fdf ? 1U : 0U;
        s_rx_rejected_id_count += rx.header.id > BSP_CAN_STANDARD_ID_MAX ? 1U : 0U;
        s_rx_rejected_dlc_count += rx.header.dlc > BSP_CAN_MAX_DLC ? 1U : 0U;
        s_last_rejected_id = rx.header.id;
        s_last_rejected_dlc = rx.header.dlc;
        s_last_rejected_flags = (rx.header.ide ? 1U : 0U) |
                                (rx.header.rtr ? 2U : 0U) |
                                (rx.header.fdf ? 4U : 0U);
        return false;
    }

    bsp_can_frame_t frame = {
        .id = (uint16_t)rx.header.id,
        .dlc = (uint8_t)rx.header.dlc,
        .is_remote = rx.header.rtr,
    };
    if (frame.dlc > 0U) {
        memcpy(frame.data, data, frame.dlc);
    }

    /* Gui khong block tu ISR. Queue day thi dem drop de Task co the bao loi. */
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (xQueueSendFromISR(s_rx_queue, &frame, &higher_priority_task_woken) != pdPASS) {
        ++s_rx_queue_overflow_count;
    } else {
        ++s_rx_enqueued_count;
    }
    return higher_priority_task_woken == pdTRUE;
}

/* Error detail duoc driver luu trong status/record. Callback nay co mat de
 * khoi dong event path va tuyet doi khong log trong ISR. */
static IRAM_ATTR bool can_error_callback(twai_node_handle_t handle,
                                         const twai_error_event_data_t *event,
                                         void *user_context)
{
    (void)handle;
    (void)user_context;
    ++s_error_callback_count;
    s_last_error_flags = event->err_flags.val;
    s_arbitration_lost_count += event->err_flags.arb_lost ? 1U : 0U;
    s_bit_error_count += event->err_flags.bit_err ? 1U : 0U;
    s_form_error_count += event->err_flags.form_err ? 1U : 0U;
    s_stuff_error_count += event->err_flags.stuff_err ? 1U : 0U;
    s_ack_error_count += event->err_flags.ack_err ? 1U : 0U;
    return false;
}

static IRAM_ATTR bool can_tx_done_callback(twai_node_handle_t handle,
                                            const twai_tx_done_event_data_t *event,
                                            void *user_context)
{
    (void)handle;
    (void)user_context;
    if (event->is_tx_success) ++s_tx_success_count;
    else ++s_tx_failed_count;
    return false;
}

/* Chuyen enum ESP-IDF sang enum BSP de Task khong phu thuoc header TWAI. */
static bsp_can_state_t map_state(twai_error_state_t state)
{
    switch (state) {
    case TWAI_ERROR_ACTIVE:
        return BSP_CAN_STATE_ACTIVE;
    case TWAI_ERROR_WARNING:
        return BSP_CAN_STATE_WARNING;
    case TWAI_ERROR_PASSIVE:
        return BSP_CAN_STATE_PASSIVE;
    case TWAI_ERROR_BUS_OFF:
        return BSP_CAN_STATE_BUS_OFF;
    default:
        return BSP_CAN_STATE_STOPPED;
    }
}

esp_err_t bsp_can_init(void)
{
    /* Idempotent: chi co mot TWAI controller tren ESP32-S3 board nay. */
    if (s_node != NULL) {
        return ESP_OK;
    }

    /* Tao queue truoc khi enable ISR; rollback neu bat ky buoc sau loi. */
    s_rx_queue = xQueueCreate(BSP_CAN_RX_QUEUE_DEPTH, sizeof(bsp_can_frame_t));
    if (s_rx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_tx_mutex = xSemaphoreCreateMutex();
    if (s_tx_mutex == NULL) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    /* GPIO2/GPIO3 la mapping co dinh cua transceiver CAN cach ly tren board.
     * 80 percent sample point phu hop Classic CAN 250 kbps. */
    const twai_onchip_node_config_t node_config = {
        .io_cfg = {
            .tx = BSP_CAN_TX_GPIO,
            .rx = BSP_CAN_RX_GPIO,
            .quanta_clk_out = GPIO_NUM_NC,
            .bus_off_indicator = GPIO_NUM_NC,
        },
        .bit_timing = {
            .bitrate = BSP_CAN_BITRATE_HZ,
            .sp_permill = 800U,
        },
        .fail_retry_cnt = 3,
        .tx_queue_depth = BSP_CAN_TX_QUEUE_DEPTH,
    };

    esp_err_t err = twai_new_node_onchip(&node_config, &s_node);
    if (err != ESP_OK) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
        vSemaphoreDelete(s_tx_mutex);
        s_tx_mutex = NULL;
        return err;
    }

    /* Register callback truoc enable de khong bo sot frame den ngay sau start. */
    const twai_event_callbacks_t callbacks = {
        .on_tx_done = can_tx_done_callback,
        .on_rx_done = can_rx_done_callback,
        .on_error = can_error_callback,
    };
    err = twai_node_register_event_callbacks(s_node, &callbacks, NULL);
    if (err == ESP_OK) {
        err = twai_node_enable(s_node);
    }
    if (err != ESP_OK) {
        (void)twai_node_delete(s_node);
        s_node = NULL;
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
        vSemaphoreDelete(s_tx_mutex);
        s_tx_mutex = NULL;
        return err;
    }

    s_rx_queue_overflow_count = 0;
    s_tx_success_count = s_tx_failed_count = 0;
    s_rx_callback_count = s_rx_enqueued_count = s_rx_read_failed_count = 0;
    s_rx_rejected_count = s_rx_rejected_ide_count = s_rx_rejected_rtr_count = 0;
    s_rx_rejected_fdf_count = s_rx_rejected_id_count = s_rx_rejected_dlc_count = 0;
    s_last_rejected_id = s_last_rejected_dlc = s_last_rejected_flags = 0;
    s_error_callback_count = s_arbitration_lost_count = s_bit_error_count = 0;
    s_form_error_count = s_stuff_error_count = s_ack_error_count = s_last_error_flags = 0;
    ESP_LOGI(TAG, "CAN ready: Classic 250 kbps, TX GPIO%d RX GPIO%d",
             BSP_CAN_TX_GPIO, BSP_CAN_RX_GPIO);
    return ESP_OK;
}

esp_err_t bsp_can_deinit(void)
{
    /* Disable truoc delete de driver dung ISR va hang doi TX an toan. */
    if (s_node == NULL) {
        return ESP_OK;
    }

    esp_err_t err = twai_node_disable(s_node);
    if (err != ESP_OK) {
        return err;
    }
    err = twai_node_delete(s_node);
    if (err != ESP_OK) {
        return err;
    }
    s_node = NULL;
    vQueueDelete(s_rx_queue);
    s_rx_queue = NULL;
    vSemaphoreDelete(s_tx_mutex);
    s_tx_mutex = NULL;
    s_rx_queue_overflow_count = 0;
    return ESP_OK;
}

esp_err_t bsp_can_send(const bsp_can_frame_t *frame, uint32_t timeout_ms)
{
    /* Validate o bien BSP: protocol layer khong the gui Extended ID/DLC > 8. */
    if (s_node == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame == NULL || frame->id > BSP_CAN_STANDARD_ID_MAX ||
        frame->dlc > BSP_CAN_MAX_DLC) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Header mac dinh ide/rtr/fdf bang 0: Standard Classic CAN data frame. */
    twai_frame_t tx = {
        .header = {
            .id = frame->id,
            .dlc = frame->dlc,
            .rtr = frame->is_remote,
        },
        .buffer = (uint8_t *)frame->data,
        .buffer_len = frame->dlc,
    };
    const TickType_t lock_timeout = pdMS_TO_TICKS(timeout_ms == 0U ? 100U : timeout_ms);
    if (s_tx_mutex == NULL || xSemaphoreTake(s_tx_mutex, lock_timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = twai_node_transmit(s_node, &tx, (int)timeout_ms);
    if (err == ESP_OK) {
        /* Do not return until the driver has stopped dereferencing `tx`. */
        const uint32_t wait_ms = timeout_ms == 0U ? 100U : timeout_ms;
        err = twai_node_transmit_wait_all_done(s_node, (int)wait_ms);
    }
    xSemaphoreGive(s_tx_mutex);
    return err;
}

esp_err_t bsp_can_receive(bsp_can_frame_t *frame, uint32_t timeout_ms)
{
    /* Chuyen timeout milliseconds cua public API sang FreeRTOS tick. */
    if (s_rx_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return xQueueReceive(s_rx_queue, frame, pdMS_TO_TICKS(timeout_ms)) == pdPASS
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

void bsp_can_get_status(bsp_can_status_t *status)
{
    /* Snapshot: status cho phep Task phat hien warning/passive/bus-off ma BSP
     * khong tu dua ra quyet dinh van hanh. */
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->initialized = s_node != NULL;
    status->state = BSP_CAN_STATE_STOPPED;
    status->rx_queue_overflow_count = s_rx_queue_overflow_count;
    status->tx_success_count = s_tx_success_count;
    status->tx_failed_count = s_tx_failed_count;
    status->rx_callback_count = s_rx_callback_count;
    status->rx_enqueued_count = s_rx_enqueued_count;
    status->rx_read_failed_count = s_rx_read_failed_count;
    status->rx_rejected_count = s_rx_rejected_count;
    status->rx_rejected_ide_count = s_rx_rejected_ide_count;
    status->rx_rejected_rtr_count = s_rx_rejected_rtr_count;
    status->rx_rejected_fdf_count = s_rx_rejected_fdf_count;
    status->rx_rejected_id_count = s_rx_rejected_id_count;
    status->rx_rejected_dlc_count = s_rx_rejected_dlc_count;
    status->last_rejected_id = s_last_rejected_id;
    status->last_rejected_dlc = s_last_rejected_dlc;
    status->last_rejected_flags = s_last_rejected_flags;
    status->error_callback_count = s_error_callback_count;
    status->arbitration_lost_count = s_arbitration_lost_count;
    status->bit_error_count = s_bit_error_count;
    status->form_error_count = s_form_error_count;
    status->stuff_error_count = s_stuff_error_count;
    status->ack_error_count = s_ack_error_count;
    status->last_error_flags = s_last_error_flags;
    if (s_node == NULL) {
        return;
    }

    twai_node_status_t node_status = { 0 };
    twai_node_record_t record = { 0 };
    if (twai_node_get_info(s_node, &node_status, &record) == ESP_OK) {
        status->state = map_state(node_status.state);
        status->tx_error_count = node_status.tx_error_count;
        status->rx_error_count = node_status.rx_error_count;
        status->bus_error_count = record.bus_err_num;
    }
}

esp_err_t bsp_can_recover(void)
{
    /* TWAI chi cho phep recover tu BUS-OFF. Goi khi ACTIVE la no-op hop le. */
    if (s_node == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    twai_node_status_t status = { 0 };
    esp_err_t err = twai_node_get_info(s_node, &status, NULL);
    if (err != ESP_OK) {
        return err;
    }
    return status.state == TWAI_ERROR_BUS_OFF ? twai_node_recover(s_node) : ESP_OK;
}
