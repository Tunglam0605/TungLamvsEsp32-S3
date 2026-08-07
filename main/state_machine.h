/**
 * @file state_machine.h
 * @brief Mission Manager public interface.
 *
 * Button and MQTT ingress never mutate a mission directly. The Mission
 * Manager owns mission transitions and outbound call/cancel transactions.
 */
#ifndef CALLBOX_STATE_MACHINE_H
#define CALLBOX_STATE_MACHINE_H

#include <stdint.h>
#include "mission_types.h"

void state_machine_init(void);
void state_machine_task(void *pvParameters);

/* Button 1/2 requests a call; button 3 requests cancel of the latest
 * cancelable mission. Admission and retry are owned by the manager. */
void handle_button_press(int button_id, uint32_t timestamp);

/* Legacy adapter retained for source compatibility. It deliberately ignores
 * command data without a ref_seq; app_event_queue is the supported ingress. */
void handle_mqtt_command(const char *cmd_type, int task_id,
                         const char *agv_id, uint32_t timestamp);

TaskState_t get_task_state(int task_id);
const char *get_task_state_str(int task_id);
const char *get_comm_state_str(void);
void reset_task(int task_id);

#endif /* CALLBOX_STATE_MACHINE_H */
