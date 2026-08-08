/**
 * @file sequence_service.h
 * @brief Bộ cấp phát số thứ tự sự kiện toàn thiết bị, lưu bền (durable).
 *
 * Một số thứ tự định danh một giao dịch sự kiện logic. Mỗi sự kiện mới nhận
 * một giá trị mới; các lần truyền lại giữ nguyên giá trị do giao dịch lưu.
 */
#ifndef CALLBOX_SEQUENCE_SERVICE_H
#define CALLBOX_SEQUENCE_SERVICE_H

#include <stdint.h>
#include "esp_err.h"

/** Nạp high-watermark đã lưu. Gọi một lần sau nvs_storage_init(). */
esp_err_t sequence_service_init(void);

/** Cấp phát và lưu bền vững số thứ tự sự kiện toàn thiết bị tiếp theo. */
esp_err_t sequence_next(uint32_t *sequence);

/** Trả về số thứ tự đã lưu thành công gần nhất. */
uint32_t sequence_current(void);

#endif /* CALLBOX_SEQUENCE_SERVICE_H */
