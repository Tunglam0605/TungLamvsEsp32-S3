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

#define BSP_CAN_TX_GPIO          GPIO_NUM_2
#define BSP_CAN_RX_GPIO          GPIO_NUM_3
#define BSP_CAN_RX_QUEUE_DEPTH   32U
#define BSP_CAN_TX_QUEUE_DEPTH   16U

static const char *TAG = "BSP_CAN";

/* s_node la owner duy nhat cua TWAI controller. s_rx_queue la cau noi ISR
 * sang Task; khong expose QueueHandle ra ngoai de BSP giu ownership. */
static twai_node_handle_t s_node;
static QueueHandle_t s_rx_queue;
/* Tang trong ISR khi RX nhanh hon Task consumer; chi dung de chan doan. */
static volatile uint32_t s_rx_queue_overflow_count;

/* Callback chay trong ISR context. Khong duoc dung API block, malloc, log hay
 * decode protocol o day. Driver yeu cau twai_node_receive_from_isr() duoc goi
 * ngay trong callback de lay frame vua nhan tu hardware FIFO. */
static IRAM_ATTR bool can_rx_done_callback(twai_node_handle_t handle,
                                           const twai_rx_done_event_data_t *event,
                                           void *user_context)
{
    (void)event;
    (void)user_context;

    /* Buffer stack co kich thuoc Classic CAN toi da 8 byte. */
    uint8_t data[BSP_CAN_MAX_DLC] = { 0 };
    twai_frame_t rx = {
        .buffer = data,
        .buffer_len = sizeof(data),
    };
    /* BSP chi chuyen Standard Classic Data frame. Extended/RTR/FD duoc loai
     * ngay tai bien hardware de application khong nham protocol. */
    if (twai_node_receive_from_isr(handle, &rx) != ESP_OK ||
        rx.header.ide || rx.header.rtr || rx.header.fdf ||
        rx.header.id > BSP_CAN_STANDARD_ID_MAX ||
        rx.header.dlc > BSP_CAN_MAX_DLC) {
        return false;
    }

    bsp_can_frame_t frame = {
        .id = (uint16_t)rx.header.id,
        .dlc = (uint8_t)rx.header.dlc,
    };
    if (frame.dlc > 0U) {
        memcpy(frame.data, data, frame.dlc);
    }

    /* Gui khong block tu ISR. Queue day thi dem drop de Task co the bao loi. */
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (xQueueSendFromISR(s_rx_queue, &frame, &higher_priority_task_woken) != pdPASS) {
        ++s_rx_queue_overflow_count;
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
    (void)event;
    (void)user_context;
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
        return err;
    }

    /* Register callback truoc enable de khong bo sot frame den ngay sau start. */
    const twai_event_callbacks_t callbacks = {
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
        return err;
    }

    s_rx_queue_overflow_count = 0;
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
        },
        .buffer = (uint8_t *)frame->data,
        .buffer_len = frame->dlc,
    };
    return twai_node_transmit(s_node, &tx, (int)timeout_ms);
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
