/**
 * @file    callbox_sews.c
 * @brief   Entrypoint ESP-IDF mỏng (app_main) — mọi bootstrap sản phẩm
 *          (NVS → config → sequence → BSP → Wi-Fi/SNTP/Ethernet → MQTT →
 *          task runtime → vòng lặp chính) nằm trong component callbox,
 *          xem callbox_app.c.
 *
 * @see     callbox_app.h — callbox_app_run()
 */
#include "callbox_app.h"

void app_main(void)
{
    callbox_app_run();
}
