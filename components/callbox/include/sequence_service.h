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

/** Nạp high-watermark đã lưu (qua sequence_store). Gọi một lần sau nvs_storage_init(). */
esp_err_t sequence_service_init(void);

/**
 * Cấp phát số thứ tự tiếp theo. Service reserve bền vững theo block 64 số:
 * runtime vẫn liên tiếp; reboot có thể bỏ qua đuôi block nhưng không reuse.
 */
esp_err_t sequence_next(uint32_t *sequence);

/** Trả về số thứ tự runtime đã cấp gần nhất (không phải cuối block reserve). */
uint32_t sequence_current(void);

#endif /* CALLBOX_SEQUENCE_SERVICE_H */
