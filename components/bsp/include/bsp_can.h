/**
 * @file bsp_can.h
 * @brief Giao dien CAN Classic cap board cho Waveshare ESP32-S3 8DI/8DO.
 *
 * Board da co san transceiver CAN cach ly. BSP so huu TWAI controller va
 * chuyen frame RX tu ISR sang hang doi FreeRTOS; lop Task lay frame bang API
 * dong bo. BSP khong phan tich CAN ID, payload, LaserID hay bat ky quy tac
 * nghiep vu nao.
 *
 * +----------------+------------------+----------------------------------+
 * | Tin hieu       | GPIO ESP32-S3    | Vai tro                          |
 * +----------------+------------------+----------------------------------+
 * | CAN_TX         | GPIO2            | TWAI TX -> transceiver cach ly   |
 * | CAN_RX         | GPIO3            | transceiver cach ly -> TWAI RX   |
 * +----------------+------------------+----------------------------------+
 *
 * Cau hinh co dinh cua BSP: Classic CAN, 250 kbps, Standard ID 11-bit,
 * Data frame DLC 0..8. Day la transport cua board, khong phai CAN-FD.
 *
 * @note    CAN_H, CAN_L va termination 120 ohm la phan lap dat bus ben ngoai;
 *          firmware khong the tu xac minh cac dieu kien vat ly nay.
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 * @see     esp_twai.h - driver TWAI moi cua ESP-IDF
 */
#ifndef BSP_CAN_H
#define BSP_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_CAN_BITRATE_HZ       250000U
#define BSP_CAN_STANDARD_ID_MAX  0x7FFU
#define BSP_CAN_MAX_DLC          8U

typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[BSP_CAN_MAX_DLC];
} bsp_can_frame_t;

typedef enum {
    BSP_CAN_STATE_STOPPED = 0,
    BSP_CAN_STATE_ACTIVE,
    BSP_CAN_STATE_WARNING,
    BSP_CAN_STATE_PASSIVE,
    BSP_CAN_STATE_BUS_OFF,
} bsp_can_state_t;

typedef struct {
    bool initialized;
    bsp_can_state_t state;
    uint16_t tx_error_count;
    uint16_t rx_error_count;
    uint32_t bus_error_count;
    uint32_t rx_queue_overflow_count;
} bsp_can_status_t;

/**
 * @brief Khoi tao transceiver CAN board va TWAI controller 250 kbps.
 *
 * Tao hang doi RX, dang ky callback ISR, enable controller. Ham idempotent:
 * goi lai sau khi thanh cong se tra ESP_OK va khong cap phat them tai nguyen.
 *
 * @return ESP_OK neu CAN san sang; ESP_ERR_NO_MEM neu khong tao duoc queue;
 *         ma loi TWAI neu GPIO/controller khong khoi tao duoc.
 */
esp_err_t bsp_can_init(void);

/**
 * @brief Dung CAN va giai phong controller cung hang doi RX.
 * @return ESP_OK neu da dung thanh cong; ESP_OK neu chua khoi tao.
 */
esp_err_t bsp_can_deinit(void);

/**
 * @brief Dua mot standard data frame vao hang doi TX cua TWAI.
 * @param[in] frame Frame co ID 0..0x7FF, DLC 0..8 va payload tuong ung.
 * @param[in] timeout_ms Thoi gian cho neu hang doi TX day; 0 la khong cho.
 * @return ESP_OK neu driver nhan frame; ESP_ERR_INVALID_STATE neu CAN chua
 *         khoi tao; ESP_ERR_INVALID_ARG neu ID/DLC khong hop le.
 * @note Ket qua ESP_OK chi xac nhan frame da vao driver, khong la ACK cua
 *       node CAN phia ben kia. Protocol layer tu quyet dinh retry/timeout.
 */
esp_err_t bsp_can_send(const bsp_can_frame_t *frame, uint32_t timeout_ms);

/**
 * @brief Lay mot standard data frame da duoc ISR dua vao hang doi RX.
 * @param[out] frame Noi nhan frame.
 * @param[in] timeout_ms Thoi gian cho frame; 0 la polling khong chan task.
 * @return ESP_OK neu co frame; ESP_ERR_TIMEOUT neu het thoi gian cho.
 */
esp_err_t bsp_can_receive(bsp_can_frame_t *frame, uint32_t timeout_ms);

/**
 * @brief Doc suc khoe TWAI va thong ke hang doi RX.
 * @param[out] status Cau truc ket qua; NULL thi khong lam gi.
 * @note rx_queue_overflow_count > 0 nghia la task tieu thu RX qua cham.
 */
void bsp_can_get_status(bsp_can_status_t *status);

/**
 * @brief Bat dau recovery khi TWAI dang BUS-OFF.
 * @return ESP_OK neu recovery da bat dau hoac controller khong BUS-OFF.
 * @note Ham nay khong tu retry frame da mat va khong dua ra quyet dinh safety.
 */
esp_err_t bsp_can_recover(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_CAN_H */
