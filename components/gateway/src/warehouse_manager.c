#include "warehouse_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "platform_nvs.h"

#define NS "warehouse_v3"
#define DISTANCE_MAX_MM 1200U

static laser_profile_t s_profile = LASER_PROFILE_GROUP_8;
static warehouse_position_config_t s_positions[WAREHOUSE_POSITION_MAX];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static bool valid_text(const char *text, size_t capacity, bool empty_ok)
{
    if (text == NULL) return false;
    size_t n = strnlen(text, capacity);
    if (n >= capacity || (!empty_ok && n == 0U)) return false;
    for (size_t i = 0; i < n; ++i) if ((unsigned char)text[i] < 0x20U || text[i] == '|') return false;
    return true;
}

static void key_for_group(uint8_t group_id, char key[8])
{
    snprintf(key, 8, "g%02u", group_id);
}

static esp_err_t persist_all(void)
{
    platform_nvs_handle_t h = {0};
    esp_err_t e = platform_nvs_open(&h, NS, false);
    if (e != ESP_OK) return e;
    e = platform_nvs_set_u8(&h, "profile", (uint8_t)s_profile);
    for (uint8_t i = 0; e == ESP_OK && i < WAREHOUSE_POSITION_MAX; ++i) {
        char key[8], value[96]; key_for_group(i + 1U, key);
        const warehouse_position_config_t *p = &s_positions[i];
        int n = snprintf(value, sizeof(value), "%u|%u|%u|%u|%u|%u|%s|%s",
            p->enabled, p->laser_id, p->distance_mm, p->distance_emergency_mm,
            p->low_col, p->high_row, p->warehouse_code, p->warehouse_name);
        if (n < 0 || (size_t)n >= sizeof(value)) { e = ESP_ERR_INVALID_SIZE; break; }
        e = platform_nvs_set_string(&h, key, value);
    }
    if (e == ESP_OK) e = platform_nvs_commit(&h);
    platform_nvs_close(&h);
    return e;
}

static bool decode(uint8_t group_id, char *value, warehouse_position_config_t *p)
{
    char *part[8] = {value};
    for (int i = 1; i < 8; ++i) { part[i] = strchr(part[i-1], '|'); if (!part[i]) return false; *part[i]++ = 0; }
    memset(p, 0, sizeof(*p)); p->group_id = group_id;
    p->enabled = strtoul(part[0], NULL, 10) != 0; p->laser_id = strtoul(part[1], NULL, 10);
    p->distance_mm = strtoul(part[2], NULL, 10); p->distance_emergency_mm = strtoul(part[3], NULL, 10);
    p->low_col = strtoul(part[4], NULL, 10); p->high_row = strtoul(part[5], NULL, 10);
    p->proximity_enabled = p->enabled;
    strlcpy(p->warehouse_code, part[6], sizeof(p->warehouse_code));
    strlcpy(p->warehouse_name, part[7], sizeof(p->warehouse_name));
    return true;
}

esp_err_t warehouse_manager_init(void)
{
    memset(s_positions, 0, sizeof(s_positions));
    for (uint8_t i=0;i<WAREHOUSE_POSITION_MAX;i++) s_positions[i].group_id=i+1U;
    platform_nvs_handle_t h={0}; esp_err_t e=platform_nvs_open(&h,NS,true);
    if (e == ESP_ERR_NOT_FOUND) return ESP_OK;
    if (e != ESP_OK) return e;
    bool found=false; uint8_t profile=0;
    if(platform_nvs_get_u8(&h,"profile",&profile,&found)==ESP_OK&&found&&laser_profile_valid((laser_profile_t)profile))s_profile=(laser_profile_t)profile;
    for(uint8_t g=1;g<=WAREHOUSE_POSITION_MAX;g++){char key[8],v[96]={0};key_for_group(g,key);found=false;e=platform_nvs_get_string(&h,key,v,sizeof(v),&found);if(e!=ESP_OK)break;if(found&&v[0]){warehouse_position_config_t p;if(decode(g,v,&p))s_positions[g-1U]=p;}}
    platform_nvs_close(&h);
    if (e == ESP_OK) (void)laser_can_bringup_set_profile(s_profile);
    return e;
}

laser_profile_t warehouse_manager_profile(void){taskENTER_CRITICAL(&s_mux);laser_profile_t p=s_profile;taskEXIT_CRITICAL(&s_mux);return p;}

warehouse_validation_t warehouse_manager_validate_profile(laser_profile_t profile)
{
    if(!laser_profile_valid(profile))return WAREHOUSE_INVALID_GROUP;
    uint8_t count=laser_profile_group_count(profile);
    taskENTER_CRITICAL(&s_mux);
    for(uint8_t i=0;i<WAREHOUSE_POSITION_MAX;i++){const warehouse_position_config_t *p=&s_positions[i];if(p->enabled&&(p->group_id>count||!laser_profile_id_allowed(profile,p->group_id,p->laser_id))){taskEXIT_CRITICAL(&s_mux);return WAREHOUSE_PROFILE_CONFLICT;}}
    taskEXIT_CRITICAL(&s_mux);return WAREHOUSE_VALID;
}

esp_err_t warehouse_manager_set_profile(laser_profile_t profile,bool clear_conflicts)
{
    if(!laser_profile_valid(profile))return ESP_ERR_INVALID_ARG;
    if(!clear_conflicts&&warehouse_manager_validate_profile(profile)!=WAREHOUSE_VALID)return ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_mux);s_profile=profile;if(clear_conflicts){uint8_t n=laser_profile_group_count(profile);for(uint8_t i=0;i<WAREHOUSE_POSITION_MAX;i++)if(s_positions[i].enabled&&(s_positions[i].group_id>n||!laser_profile_id_allowed(profile,s_positions[i].group_id,s_positions[i].laser_id)))memset(&s_positions[i],0,sizeof(s_positions[i])),s_positions[i].group_id=i+1U;}taskEXIT_CRITICAL(&s_mux);
    esp_err_t err = persist_all();
    if (err == ESP_OK) err = laser_can_bringup_set_profile(profile);
    return err;
}

warehouse_validation_t warehouse_manager_validate_position(const warehouse_position_config_t *p)
{
    if(!p||p->group_id==0||p->group_id>laser_profile_group_count(s_profile))return WAREHOUSE_INVALID_GROUP;
    if(!p->enabled)return WAREHOUSE_VALID;
    if(!laser_profile_id_allowed(s_profile,p->group_id,p->laser_id))return WAREHOUSE_INVALID_LASER_ID;
    if(p->distance_mm>DISTANCE_MAX_MM||p->distance_emergency_mm>p->distance_mm)return WAREHOUSE_INVALID_DISTANCE;
    if(!valid_text(p->warehouse_code,sizeof(p->warehouse_code),false)||!valid_text(p->warehouse_name,sizeof(p->warehouse_name),true))return WAREHOUSE_INVALID_TEXT;
    taskENTER_CRITICAL(&s_mux);
    for(uint8_t i=0;i<laser_profile_group_count(s_profile);i++){const warehouse_position_config_t *x=&s_positions[i];if(!x->enabled||x->group_id==p->group_id)continue;if(x->laser_id==p->laser_id){taskEXIT_CRITICAL(&s_mux);return WAREHOUSE_DUPLICATE_LASER_ID;}if(strcmp(x->warehouse_code,p->warehouse_code)==0){taskEXIT_CRITICAL(&s_mux);return WAREHOUSE_DUPLICATE_CODE;}}
    taskEXIT_CRITICAL(&s_mux);return WAREHOUSE_VALID;
}

esp_err_t warehouse_manager_set_position(const warehouse_position_config_t *p)
{
    warehouse_validation_t v=warehouse_manager_validate_position(p);if(v!=WAREHOUSE_VALID)return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL(&s_mux);s_positions[p->group_id-1U]=*p;taskEXIT_CRITICAL(&s_mux);return persist_all();
}

warehouse_state_t warehouse_state_from_sensor(bool online,bool status_valid,laser_obstacle_state_t warn)
{if(!online||!status_valid)return WAREHOUSE_STATE_UNKNOWN;return warn==LASER_OBSTACLE_CLEAR?WAREHOUSE_STATE_EMPTY:WAREHOUSE_STATE_OCCUPIED;}

bool warehouse_manager_get_position(uint8_t group_id,warehouse_position_t *out)
{
    if(!out||group_id==0||group_id>laser_profile_group_count(s_profile))return false;
    warehouse_position_config_t c;taskENTER_CRITICAL(&s_mux);c=s_positions[group_id-1U];taskEXIT_CRITICAL(&s_mux);
    memset(out,0,sizeof(*out));out->config=c;out->last_seen_ago_ms=-1;out->state=WAREHOUSE_STATE_UNKNOWN;
    if (!c.enabled) return true;
    laser_can_node_status_t n={0};out->sensor_detected=laser_can_bringup_get_node(c.laser_id,&n);out->sensor_online=out->sensor_detected&&n.alive;out->status_valid=out->sensor_online&&n.status_valid&&n.obstacle_valid;out->warn=n.obstacle_state;out->config_state=n.config_state;out->distance_mm=n.distance_mm;out->distance_emergency_mm=n.distance_emergency_mm;if(out->sensor_detected){int64_t now=esp_timer_get_time()/1000LL;out->last_seen_ago_ms=now>=n.last_seen_ms?now-n.last_seen_ms:-1;}out->state=warehouse_state_from_sensor(out->sensor_online,out->status_valid,out->warn);return true;
}

void warehouse_manager_snapshot(warehouse_snapshot_t *s)
{if(!s)return;memset(s,0,sizeof(*s));s->profile=warehouse_manager_profile();s->group_count=laser_profile_group_count(s->profile);for(uint8_t g=1;g<=s->group_count;g++){warehouse_manager_get_position(g,&s->positions[g-1U]);warehouse_position_t *p=&s->positions[g-1U];if(p->config.enabled)s->configured++;if(p->sensor_online)s->online++;if(p->state==WAREHOUSE_STATE_UNKNOWN)s->unknown++;else if(p->state==WAREHOUSE_STATE_EMPTY)s->empty++;else s->occupied++;}}

const char *warehouse_state_name(warehouse_state_t s){return s==WAREHOUSE_STATE_EMPTY?"EMPTY":s==WAREHOUSE_STATE_OCCUPIED?"OCCUPIED":"UNKNOWN";}
const char *warehouse_validation_name(warehouse_validation_t v){switch(v){case WAREHOUSE_INVALID_GROUP:return"INVALID_GROUP";case WAREHOUSE_INVALID_LASER_ID:return"LASER_OUT_OF_GROUP";case WAREHOUSE_DUPLICATE_LASER_ID:return"DUPLICATE_LASER_ID";case WAREHOUSE_DUPLICATE_CODE:return"DUPLICATE_WAREHOUSE_CODE";case WAREHOUSE_INVALID_DISTANCE:return"INVALID_DISTANCE";case WAREHOUSE_INVALID_TEXT:return"INVALID_TEXT";case WAREHOUSE_PROFILE_CONFLICT:return"PROFILE_CONFLICT";default:return"VALID";}}
