#include "gateway_status.h"
#include "bsp_buzzer.h"
#include "bsp_can.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gateway_mqtt.h"
#include "gateway_network.h"
#include "laser_can_bringup.h"
#include "platform_wifi.h"
#include "warehouse_manager.h"

typedef struct { uint16_t hz; uint16_t on_ms; uint16_t off_ms; } tone_step_t;
typedef struct { const tone_step_t *steps; uint8_t count; } beep_code_t;
#define S(h,on,off) {h,on,off}
static const tone_step_t CODE_AP_ON[]={S(2400,120,90),S(2400,120,90),S(2400,120,0)};
static const tone_step_t CODE_AP_OFF[]={S(1600,650,180),S(1600,650,0)};
static const tone_step_t CODE_NET_UP[]={S(2400,120,100),S(2400,120,0)};
static const tone_step_t CODE_NET_DOWN[]={S(1600,650,0)};
static const tone_step_t CODE_MQTT_UP[]={S(2400,120,120),S(1600,650,160),S(1600,650,0)};
static const tone_step_t CODE_MQTT_DOWN[]={S(1800,400,100),S(1800,400,0)};
static const tone_step_t CODE_CAN_OFF[]={S(1100,800,120),S(1100,800,0)};
static const tone_step_t CODE_RECOVERED[]={S(2600,100,80),S(2600,100,0)};
static const tone_step_t CODE_LASER_OFF[]={S(1400,300,100),S(1400,300,100),S(1400,300,0)};
static const tone_step_t CODE_MISMATCH[]={S(1000,180,80),S(2000,180,80),S(1000,180,0)};
static const beep_code_t CODES[GATEWAY_DIAG_EVENT_COUNT]={
 [GATEWAY_DIAG_AP_ON]={CODE_AP_ON,3},[GATEWAY_DIAG_AP_OFF]={CODE_AP_OFF,2},
 [GATEWAY_DIAG_NETWORK_UP]={CODE_NET_UP,2},[GATEWAY_DIAG_NETWORK_DOWN]={CODE_NET_DOWN,1},
 [GATEWAY_DIAG_MQTT_UP]={CODE_MQTT_UP,3},[GATEWAY_DIAG_MQTT_DOWN]={CODE_MQTT_DOWN,2},
 [GATEWAY_DIAG_CAN_BUS_OFF]={CODE_CAN_OFF,2},[GATEWAY_DIAG_CAN_RECOVERED]={CODE_RECOVERED,2},
 [GATEWAY_DIAG_LASER_OFFLINE]={CODE_LASER_OFF,3},[GATEWAY_DIAG_LASER_RECOVERED]={CODE_RECOVERED,2},
 [GATEWAY_DIAG_CONFIG_MISMATCH]={CODE_MISMATCH,3},
};
static const char *TAG="GW_DIAG";static QueueHandle_t s_queue;

esp_err_t gateway_diagnostic_report(gateway_diagnostic_event_t event)
{if(!s_queue||event>=GATEWAY_DIAG_EVENT_COUNT)return ESP_ERR_INVALID_STATE;return xQueueSend(s_queue,&event,0)==pdTRUE?ESP_OK:ESP_ERR_NO_MEM;}
static void buzzer_task(void*a){(void)a;gateway_diagnostic_event_t e;for(;;){if(xQueueReceive(s_queue,&e,portMAX_DELAY)!=pdTRUE)continue;const beep_code_t*c=&CODES[e];for(uint8_t i=0;i<c->count;i++){(void)bsp_buzzer_set(c->steps[i].hz,45);vTaskDelay(pdMS_TO_TICKS(c->steps[i].on_ms));(void)bsp_buzzer_off();if(c->steps[i].off_ms)vTaskDelay(pdMS_TO_TICKS(c->steps[i].off_ms));}}}
static void report_transition(bool current,bool*previous,gateway_diagnostic_event_t up,gateway_diagnostic_event_t down){if(current!=*previous){(void)gateway_diagnostic_report(current?up:down);*previous=current;}}
static void status_task(void*a)
{
 (void)a;bool init=false,network=false,mqtt=false,ap=false,can_ok=true,laser_ok=false,mismatch=false;
 for(;;){bsp_can_status_t cs={0};bsp_can_get_status(&cs);warehouse_snapshot_t ws;warehouse_manager_snapshot(&ws);bool n=gateway_network_production_available(),m=gateway_mqtt_is_connected(),p=platform_wifi_ap_is_active(),c=cs.state!=BSP_CAN_STATE_BUS_OFF,l=ws.configured==0||ws.online==ws.configured,mm=false;laser_can_node_status_t nodes[LASER_CAN_MAX_NODES];size_t count=laser_can_bringup_get_nodes(nodes,LASER_CAN_MAX_NODES);for(size_t i=0;i<count;i++)if(nodes[i].config_state==LASER_CONFIG_MISMATCH)mm=true;
 if(!init){network=n;mqtt=m;ap=false;can_ok=c;laser_ok=l;mismatch=false;init=true;}
 report_transition(n,&network,GATEWAY_DIAG_NETWORK_UP,GATEWAY_DIAG_NETWORK_DOWN);report_transition(m,&mqtt,GATEWAY_DIAG_MQTT_UP,GATEWAY_DIAG_MQTT_DOWN);report_transition(p,&ap,GATEWAY_DIAG_AP_ON,GATEWAY_DIAG_AP_OFF);report_transition(c,&can_ok,GATEWAY_DIAG_CAN_RECOVERED,GATEWAY_DIAG_CAN_BUS_OFF);report_transition(l,&laser_ok,GATEWAY_DIAG_LASER_RECOVERED,GATEWAY_DIAG_LASER_OFFLINE);if(mm&&!mismatch)(void)gateway_diagnostic_report(GATEWAY_DIAG_CONFIG_MISMATCH);mismatch=mm;vTaskDelay(pdMS_TO_TICKS(500));
 }
}
esp_err_t gateway_status_start(void){if(s_queue)return ESP_OK;s_queue=xQueueCreate(12,sizeof(gateway_diagnostic_event_t));if(!s_queue)return ESP_ERR_NO_MEM;if(xTaskCreate(buzzer_task,"gw_buzzer",3072,NULL,4,NULL)!=pdPASS||xTaskCreate(status_task,"gw_diag",4096,NULL,5,NULL)!=pdPASS)return ESP_ERR_NO_MEM;ESP_LOGI(TAG,"Transition diagnostic manager started");return ESP_OK;}
