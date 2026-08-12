#include "gateway_mqtt.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gateway_config.h"
#include "gateway_network.h"
#include "mqtt_client.h"
#include "warehouse_manager.h"

static const char *TAG="GW_MQTT";static esp_mqtt_client_handle_t s_client;static SemaphoreHandle_t s_lock;static volatile bool s_connected;static volatile bool s_publish_now;static uint32_t s_sequence;static char s_uri[128],s_client_id[40],s_status_topic[96],s_state_topic[96],s_user[48],s_password[64];
static bool tls_time_ready(void){time_t now=0;time(&now);return now>1700000000;}

static void publish_maps(void)
{
    warehouse_snapshot_t s;warehouse_manager_snapshot(&s);gateway_config_t c;gateway_config_get(&c);
    for(uint8_t i=0;i<s.group_count;i++){warehouse_position_t*p=&s.positions[i];if(!p->config.enabled)continue;char topic[112],json[240];snprintf(topic,sizeof(topic),"gateway/%s/warehouse/map/%u",c.gateway_id,p->config.group_id);int n=snprintf(json,sizeof(json),"{\"transitional\":true,\"group_id\":%u,\"laser_id\":%u,\"warehouse_code\":\"%s\",\"warehouse_name\":\"%s\"}",p->config.group_id,p->config.laser_id,p->config.warehouse_code,p->config.warehouse_name);if(n>0&&n<(int)sizeof(json))(void)esp_mqtt_client_enqueue(s_client,topic,json,n,1,true,true);}
}

static void publish_state(void)
{
    /* Transitional serializer: the production group wire contract is TBD.
     * Warehouse Manager exposes a neutral snapshot and never knows this JSON. */
    warehouse_snapshot_t s;warehouse_manager_snapshot(&s);char json[1280];size_t used=(size_t)snprintf(json,sizeof(json),"{\"schema\":\"TRANSITIONAL_GROUP_SNAPSHOT_V1\",\"sequence\":%"PRIu32",\"profile\":\"%s\",\"positions\":[",++s_sequence,laser_profile_name(s.profile));
    for(uint8_t i=0;i<s.group_count;i++){warehouse_position_t*p=&s.positions[i];int n=snprintf(json+used,sizeof(json)-used,"%s{\"group_id\":%u,\"laser_id\":%u,\"state\":\"%s\",\"online\":%s}",i?",":"",p->config.group_id,p->config.laser_id,warehouse_state_name(p->state),p->sensor_online?"true":"false");if(n<0||(size_t)n>=sizeof(json)-used)return;used+=(size_t)n;}if(used+3>=sizeof(json))return;json[used++]=']';json[used++]='}';json[used]=0;(void)esp_mqtt_client_enqueue(s_client,s_state_topic,json,(int)used,1,false,true);
}

static void mqtt_event(void*a,esp_event_base_t b,int32_t id,void*d){(void)a;(void)b;esp_mqtt_event_handle_t e=d;if(id==MQTT_EVENT_CONNECTED){s_connected=true;s_publish_now=true;(void)esp_mqtt_client_enqueue(e->client,s_status_topic,"{\"online\":true}",15,1,true,true);publish_maps();ESP_LOGI(TAG,"MQTT connected; transitional group snapshot: %s",s_state_topic);}else if(id==MQTT_EVENT_DISCONNECTED){s_connected=false;ESP_LOGW(TAG,"MQTT disconnected; CAN and warehouse remain active");}else if(id==MQTT_EVENT_ERROR)ESP_LOGW(TAG,"MQTT transport error");}
static void destroy_locked(void){s_connected=false;if(s_client){(void)esp_mqtt_client_stop(s_client);(void)esp_mqtt_client_destroy(s_client);s_client=NULL;}}
static void connect_locked(void){gateway_config_t c;gateway_config_get(&c);if(s_client||!c.mqtt_broker[0]||!gateway_network_is_connected())return;if(c.mqtt_transport==GATEWAY_MQTT_TLS&&!tls_time_ready())return;snprintf(s_uri,sizeof(s_uri),"%s://%s:%u",c.mqtt_transport==GATEWAY_MQTT_TLS?"mqtts":"mqtt",c.mqtt_broker,c.mqtt_port);snprintf(s_client_id,sizeof(s_client_id),"AUBOT-GATEWAY-%s",c.gateway_id);snprintf(s_status_topic,sizeof(s_status_topic),"gateway/%s/status",c.gateway_id);snprintf(s_state_topic,sizeof(s_state_topic),"gateway/%s/warehouse/state",c.gateway_id);strlcpy(s_user,c.mqtt_user,sizeof(s_user));strlcpy(s_password,c.mqtt_password,sizeof(s_password));esp_mqtt_client_config_t mc={.broker.address.uri=s_uri,.credentials.client_id=s_client_id,.credentials.username=s_user[0]?s_user:NULL,.credentials.authentication.password=s_password[0]?s_password:NULL,.session.keepalive=30,.session.last_will.topic=s_status_topic,.session.last_will.msg="{\"online\":false}",.session.last_will.qos=1,.session.last_will.retain=true,.network.timeout_ms=10000,.network.reconnect_timeout_ms=5000};if(c.mqtt_transport==GATEWAY_MQTT_TLS)mc.broker.verification.crt_bundle_attach=esp_crt_bundle_attach;s_client=esp_mqtt_client_init(&mc);if(!s_client)return;(void)esp_mqtt_client_register_event(s_client,MQTT_EVENT_ANY,mqtt_event,NULL);if(esp_mqtt_client_start(s_client)!=ESP_OK)destroy_locked();}
static void mqtt_task(void*a){(void)a;uint32_t maps=0;for(;;){gateway_config_t c;gateway_config_get(&c);xSemaphoreTake(s_lock,portMAX_DELAY);connect_locked();if(s_connected&&s_client){publish_state();s_publish_now=false;if(++maps>=30){publish_maps();maps=0;}}xSemaphoreGive(s_lock);vTaskDelay(pdMS_TO_TICKS(s_publish_now?50:c.publish_interval_ms));}}
esp_err_t gateway_mqtt_start(void){if(!s_lock)s_lock=xSemaphoreCreateMutex();if(!s_lock)return ESP_ERR_NO_MEM;return xTaskCreate(mqtt_task,"gw_mqtt",10240,NULL,5,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;}
void gateway_mqtt_reconfigure(void){if(!s_lock)return;xSemaphoreTake(s_lock,portMAX_DELAY);destroy_locked();connect_locked();xSemaphoreGive(s_lock);}
bool gateway_mqtt_is_connected(void){return s_connected;}const char*gateway_mqtt_state_topic(void){return s_state_topic;}
