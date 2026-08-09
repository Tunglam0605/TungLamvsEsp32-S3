/**
 * @file    callbox_app.h
 * @brief   Điểm vào ứng dụng Callbox SEWS — bootstrap sản phẩm nằm trong
 *          component callbox, main chỉ giữ entrypoint ESP-IDF mỏng.
 *
 *          Trước Phase G.2, toàn bộ bootstrap (factory config, load NVS,
 *          AP identity, callback wiring, khởi tạo BSP/Wi-Fi/SNTP/Ethernet/
 *          MQTT, tạo task runtime, vòng lặp chính) nằm trong
 *          main/callbox_sews.c. Sau G.2, việc đó thuộc callbox_app.c;
 *          main gọi callbox_app_run() từ app_main.
 *
 *          KHÔNG tạo DI framework, KHÔNG context object, KHÔNG callback
 *          bundle — firmware này có đúng MỘT instance ứng dụng CallBox.
 *
 * @author  TungLamAutomation <tunglam652004@gmail.com>
 * @version 1.0.0
 * @date    2026
 *
 * @see     callbox_app.c — triển khai bootstrap
 */
#ifndef CALLBOX_APP_H
#define CALLBOX_APP_H

/**
 * @brief Khởi động và chạy ứng dụng CallBox SEWS.
 *
 * Thực hiện khởi tạo boot của sản phẩm (NVS → config → sequence → event
 * queue → buzzer → BSP → runtime modules → Wi-Fi APSTA → SNTP → network
 * status → Ethernet → 5 s delay → MQTT → task runtime) rồi đi vào vòng
 * lặp chính. Hàm này BÌNH THƯỜNG KHÔNG TRẢ VỀ — main chỉ gọi đúng một lần
 * từ app_main.
 */
void callbox_app_run(void);

#endif /* CALLBOX_APP_H */
