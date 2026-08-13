#include "warehouse_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "platform_nvs.h"

#define NS "warehouse_v3"
#define DISTANCE_MAX_MM 1200U
#define POSITION_NVS_VALUE_LEN 108U
#define FIXED_LAYOUT_VERSION 2U

static const laser_profile_t s_profile = LASER_PROFILE_GROUP_12;
static warehouse_position_config_t s_positions[WAREHOUSE_POSITION_MAX];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static const char *TAG = "WAREHOUSE";

static bool valid_text(const char *text, size_t capacity, bool empty_ok)
{
    if (text == NULL) return false;
    size_t n = strnlen(text, capacity);
    if (n >= capacity || (!empty_ok && n == 0U)) return false;
    for (size_t i = 0; i < n; ++i) if ((unsigned char)text[i] < 0x20U || text[i] == '|') return false;
    return true;
}

static void key_for_position(uint8_t position_id, char key[8])
{
    /* Keep legacy NVS keys so existing position mappings migrate in place. */
    snprintf(key, 8, "g%02u", position_id);
}

static esp_err_t persist_all(void)
{
    laser_profile_t profile;
    /* Startup runs on ESP-IDF's relatively small main task stack. These
     * bounded workspaces are static so restoring all positions cannot exhaust
     * that stack before the application tasks are created. Calls are
     * serialized by startup or the single HTTP server task. */
    static warehouse_position_config_t positions[WAREHOUSE_POSITION_MAX];
    taskENTER_CRITICAL(&s_mux);
    profile = s_profile;
    memcpy(positions, s_positions, sizeof(positions));
    taskEXIT_CRITICAL(&s_mux);

    platform_nvs_handle_t h = {0};
    esp_err_t e = platform_nvs_open(&h, NS, false);
    if (e != ESP_OK) return e;
    e = platform_nvs_set_u8(&h, "profile", (uint8_t)profile);
    if (e == ESP_OK) e = platform_nvs_set_u8(&h, "layout_v", FIXED_LAYOUT_VERSION);
    for (uint8_t i = 0; e == ESP_OK && i < WAREHOUSE_POSITION_MAX; ++i) {
        char key[8], value[POSITION_NVS_VALUE_LEN]; key_for_position(i + 1U, key);
        const warehouse_position_config_t *p = &positions[i];
        int n = snprintf(value, sizeof(value), "%u|%u|%u|%u|%u|%u|%u|%u|%u|%s|%s",
            p->enabled, p->group_id, p->laser_id, p->distance_mm, p->distance_emergency_mm,
            p->low_col, p->high_row, p->proximity_enabled, p->config_applied,
            p->warehouse_code, p->warehouse_name);
        if (n < 0 || (size_t)n >= sizeof(value)) { e = ESP_ERR_INVALID_SIZE; break; }
        e = platform_nvs_set_string(&h, key, value);
    }
    if (e == ESP_OK) e = platform_nvs_commit(&h);
    platform_nvs_close(&h);
    return e;
}

static bool decode(uint8_t position_id, char *value, warehouse_position_config_t *p)
{
    char *part[11] = {value};
    size_t count = 1;
    while (count < 11U) {
        char *separator = strchr(part[count - 1U], '|');
        if (separator == NULL) break;
        *separator = '\0';
        part[count++] = separator + 1;
    }
    if (count != 8U && count != 9U && count != 10U && count != 11U) return false;

    memset(p, 0, sizeof(*p));
    p->position_id = position_id;
    p->enabled = strtoul(part[0], NULL, 10) != 0;
    const bool separated = count >= 10U;
    p->group_id = separated ? strtoul(part[1], NULL, 10) : position_id;
    const size_t data = separated ? 2U : 1U;
    p->laser_id = strtoul(part[data], NULL, 10);
    p->distance_mm = strtoul(part[data + 1U], NULL, 10);
    p->distance_emergency_mm = strtoul(part[data + 2U], NULL, 10);
    p->low_col = strtoul(part[data + 3U], NULL, 10);
    p->high_row = strtoul(part[data + 4U], NULL, 10);
    const size_t text_index = count == 11U ? 9U : separated ? 8U : count == 9U ? 7U : 6U;
    p->proximity_enabled = separated
        ? strtoul(part[7U], NULL, 10) != 0
        : count == 9U ? strtoul(part[6U], NULL, 10) != 0 : p->enabled;
    /* Legacy records do not prove that the operator ever pressed Apply.
     * Migrate fail-closed: the mapping remains visible, but an authorized
     * operator must commission it once before automatic replies are armed. */
    p->config_applied = count == 11U
        ? strtoul(part[8U], NULL, 10) != 0 : false;
    strlcpy(p->warehouse_code, part[text_index], sizeof(p->warehouse_code));
    strlcpy(p->warehouse_name, part[text_index + 1U], sizeof(p->warehouse_name));
    return true;
}

static esp_err_t sync_laser_configs(void)
{
    laser_profile_t profile;
    static warehouse_position_config_t positions[WAREHOUSE_POSITION_MAX];
    taskENTER_CRITICAL(&s_mux);
    profile = s_profile;
    memcpy(positions, s_positions, sizeof(positions));
    taskEXIT_CRITICAL(&s_mux);

    static laser_can_config_request_t requests[WAREHOUSE_POSITION_MAX];
    memset(requests, 0, sizeof(requests));
    size_t request_count = 0U;
    for (uint8_t index = 0U; index < WAREHOUSE_POSITION_MAX; ++index) {
        const warehouse_position_config_t *position = &positions[index];
        if (!position->enabled || !position->config_applied) {
            continue;
        }
        const warehouse_validation_t validation = warehouse_manager_validate_candidate(
            profile, positions, WAREHOUSE_POSITION_MAX, position);
        if (validation != WAREHOUSE_VALID) {
            ESP_LOGE(TAG, "Position %u stored config ignored during restore: %s",
                     position->position_id, warehouse_validation_name(validation));
            continue;
        }
        requests[request_count++] = (laser_can_config_request_t) {
            .laser_id = position->laser_id,
            .distance_mm = position->distance_mm,
            .distance_emergency_mm = position->distance_emergency_mm,
            .low_col = position->low_col,
            .high_row = position->high_row,
            .proximity_enabled = position->proximity_enabled,
        };
    }
    return laser_can_bringup_replace_configs(requests, request_count);
}

esp_err_t warehouse_manager_init(void)
{
    memset(s_positions, 0, sizeof(s_positions));
    for (uint8_t i=0;i<WAREHOUSE_POSITION_MAX;i++) s_positions[i].position_id=i+1U;
    platform_nvs_handle_t h={0}; esp_err_t e=platform_nvs_open(&h,NS,true);
    if (e == ESP_ERR_NOT_FOUND) {
        return sync_laser_configs();
    }
    if (e != ESP_OK) return e;
    bool found=false; uint8_t profile=0, layout_version=0;
    e=platform_nvs_get_u8(&h,"profile",&profile,&found);
    if (e == ESP_OK) e=platform_nvs_get_u8(&h,"layout_v",&layout_version,&found);
    for(uint8_t position=1;position<=WAREHOUSE_POSITION_MAX;position++){char key[8],v[POSITION_NVS_VALUE_LEN]={0};key_for_position(position,key);found=false;e=platform_nvs_get_string(&h,key,v,sizeof(v),&found);if(e!=ESP_OK)break;if(found&&v[0]){warehouse_position_config_t p;if(decode(position,v,&p))s_positions[position-1U]=p;}}
    platform_nvs_close(&h);
    /* Migrate an installed 8-position unit in place. The physical Laser ID is
     * authoritative for the fixed 12-group layout, so keep its warehouse
     * position and commissioning data while updating only the group number.
     * This preserves distance, 8x8 masks and config_applied across upgrade. */
    bool migrated = profile != (uint8_t)LASER_PROFILE_GROUP_12 ||
                    layout_version != FIXED_LAYOUT_VERSION;
    if (e == ESP_OK && migrated) {
        bool used_group[LASER_PROFILE_MAX_GROUPS + 1U] = {false};
        bool used_laser[LASER_ID_MAX + 1U] = {false};
        for (uint8_t i = 0U; i < WAREHOUSE_POSITION_MAX; ++i) {
            warehouse_position_config_t *position = &s_positions[i];
            uint8_t group_id = 0U;
            if (!position->enabled) continue;
            if (!laser_profile_group_for_id(LASER_PROFILE_GROUP_12,
                                            position->laser_id, &group_id) ||
                used_group[group_id] || used_laser[position->laser_id]) {
                ESP_LOGW(TAG, "Discarding duplicate/invalid legacy mapping at position %u",
                         position->position_id);
                memset(position, 0, sizeof(*position));
                position->position_id = i + 1U;
                continue;
            }
            position->group_id = group_id;
            used_group[group_id] = true;
            used_laser[position->laser_id] = true;
        }
        e = persist_all();
        if (e == ESP_OK) {
            ESP_LOGI(TAG, "Migrated stored warehouse layout to fixed 12 positions");
        }
    }
    if (e == ESP_OK) e = sync_laser_configs();
    return e;
}

laser_profile_t warehouse_manager_profile(void){return LASER_PROFILE_GROUP_12;}

warehouse_validation_t warehouse_manager_validate_candidate(
    laser_profile_t profile, const warehouse_position_config_t *positions,
    size_t position_count, const warehouse_position_config_t *p)
{
    if (!p || (positions == NULL && position_count != 0U)) return WAREHOUSE_INVALID_GROUP;
    const uint8_t group_count = laser_profile_group_count(profile);
    if(p->position_id==0||p->position_id>group_count)return WAREHOUSE_INVALID_GROUP;
    if(!p->enabled)return WAREHOUSE_VALID;
    if(p->group_id==0||p->group_id>group_count)return WAREHOUSE_INVALID_GROUP;
    if(!laser_profile_id_allowed(profile,p->group_id,p->laser_id))return WAREHOUSE_INVALID_LASER_ID;
    if(p->distance_mm>DISTANCE_MAX_MM||p->distance_emergency_mm>p->distance_mm)return WAREHOUSE_INVALID_DISTANCE;
    if(!valid_text(p->warehouse_code,sizeof(p->warehouse_code),false)||!valid_text(p->warehouse_name,sizeof(p->warehouse_name),true))return WAREHOUSE_INVALID_TEXT;
    if (position_count > group_count) position_count = group_count;
    for(size_t i=0;i<position_count;i++){const warehouse_position_config_t *x=&positions[i];if(!x->enabled||x->position_id==p->position_id)continue;if(x->group_id==p->group_id)return WAREHOUSE_DUPLICATE_GROUP;if(x->laser_id==p->laser_id)return WAREHOUSE_DUPLICATE_LASER_ID;if(strcmp(x->warehouse_code,p->warehouse_code)==0)return WAREHOUSE_DUPLICATE_CODE;}
    return WAREHOUSE_VALID;
}

warehouse_validation_t warehouse_manager_validate_position(const warehouse_position_config_t *p)
{
    laser_profile_t profile;
    warehouse_position_config_t positions[WAREHOUSE_POSITION_MAX];
    taskENTER_CRITICAL(&s_mux);
    profile = s_profile;
    memcpy(positions, s_positions, sizeof(positions));
    taskEXIT_CRITICAL(&s_mux);
    const warehouse_validation_t validation = warehouse_manager_validate_candidate(
        profile, positions, WAREHOUSE_POSITION_MAX, p);
    if (validation != WAREHOUSE_VALID || !p->enabled) return validation;
    laser_group_definition_t definition={0};
    if (!laser_profile_group_definition(profile,p->group_id,&definition))
        return WAREHOUSE_INVALID_GROUP;
    for(uint8_t id=definition.laser_id_first;id<=definition.laser_id_last;++id){
        laser_can_node_status_t node={0};
        if(laser_can_bringup_get_node(id,&node)&&node.alive)return WAREHOUSE_VALID;
    }
    return WAREHOUSE_GROUP_OFFLINE;
}

esp_err_t warehouse_manager_set_position(const warehouse_position_config_t *p)
{
    warehouse_validation_t v=warehouse_manager_validate_position(p);if(v!=WAREHOUSE_VALID)return ESP_ERR_INVALID_ARG;
    warehouse_position_config_t previous;
    warehouse_position_config_t next=*p;
    taskENTER_CRITICAL(&s_mux);
    previous=s_positions[p->position_id-1U];
    const bool controls_unchanged=previous.enabled&&next.enabled&&
        previous.group_id==next.group_id&&previous.laser_id==next.laser_id&&
        previous.distance_mm==next.distance_mm&&
        previous.distance_emergency_mm==next.distance_emergency_mm&&
        previous.low_col==next.low_col&&previous.high_row==next.high_row&&
        previous.proximity_enabled==next.proximity_enabled;
    next.config_applied=controls_unchanged&&previous.config_applied;
    s_positions[p->position_id-1U]=next;
    taskEXIT_CRITICAL(&s_mux);
    esp_err_t err=persist_all();
    if(err!=ESP_OK){
        taskENTER_CRITICAL(&s_mux);
        s_positions[p->position_id-1U]=previous;
        taskEXIT_CRITICAL(&s_mux);
        return err;
    }
    err=sync_laser_configs();
    if(err!=ESP_OK){
        taskENTER_CRITICAL(&s_mux);
        s_positions[p->position_id-1U]=previous;
        taskEXIT_CRITICAL(&s_mux);
        (void)persist_all();
        (void)sync_laser_configs();
    }
    return err;
}

esp_err_t warehouse_manager_mark_config_applied(uint8_t position_id)
{
    if(position_id==0U||position_id>laser_profile_group_count(warehouse_manager_profile()))
        return ESP_ERR_INVALID_ARG;
    warehouse_position_config_t previous;
    taskENTER_CRITICAL(&s_mux);
    previous=s_positions[position_id-1U];
    if(!previous.enabled){taskEXIT_CRITICAL(&s_mux);return ESP_ERR_INVALID_STATE;}
    s_positions[position_id-1U].config_applied=true;
    taskEXIT_CRITICAL(&s_mux);
    esp_err_t err=persist_all();
    if(err==ESP_OK)err=sync_laser_configs();
    if(err!=ESP_OK){
        taskENTER_CRITICAL(&s_mux);
        s_positions[position_id-1U]=previous;
        taskEXIT_CRITICAL(&s_mux);
        (void)persist_all();
        (void)sync_laser_configs();
    }
    return err;
}

warehouse_state_t warehouse_state_from_sensor(bool online,bool status_valid,laser_obstacle_state_t warn)
{if(!online||!status_valid)return WAREHOUSE_STATE_UNKNOWN;return warn==LASER_OBSTACLE_CLEAR?WAREHOUSE_STATE_EMPTY:WAREHOUSE_STATE_OCCUPIED;}

bool warehouse_manager_get_position(uint8_t position_id,warehouse_position_t *out)
{
    if(!out||position_id==0)return false;
    warehouse_position_config_t c = {0};
    laser_profile_t profile;
    taskENTER_CRITICAL(&s_mux);profile=s_profile;if(position_id<=laser_profile_group_count(profile))c=s_positions[position_id-1U];taskEXIT_CRITICAL(&s_mux);
    if(position_id>laser_profile_group_count(profile))return false;
    memset(out,0,sizeof(*out));out->config=c;out->last_seen_ago_ms=-1;out->state=WAREHOUSE_STATE_UNKNOWN;
    if (!c.enabled) return true;
    laser_group_definition_t definition = {0};
    if (!laser_profile_group_definition(profile, c.group_id, &definition)) return true;
    laser_can_node_status_t selected={0};
    out->sensor_detected=laser_can_bringup_get_node(c.laser_id,&selected);
    int64_t newest_seen = -1;
    for (uint8_t id=definition.laser_id_first; id<=definition.laser_id_last; ++id) {
        laser_can_node_status_t node={0};
        if (!laser_can_bringup_get_node(id,&node)) continue;
        out->sensor_detected=true;
        if (node.alive) {
            out->sensor_online=true;
            if (node.last_seen_ms > newest_seen) newest_seen=node.last_seen_ms;
        }
    }
    laser_can_group_status_t group={0};
    if (laser_can_bringup_get_group((uint8_t)(c.group_id-1U),&group)) out->warn=group.state;
    out->status_valid=out->sensor_online;
    out->config_state=selected.config_state;
    out->distance_mm=selected.distance_mm;
    out->distance_emergency_mm=selected.distance_emergency_mm;
    if(newest_seen>=0){int64_t now=esp_timer_get_time()/1000LL;out->last_seen_ago_ms=now>=newest_seen?now-newest_seen:-1;}
    out->state=warehouse_state_from_sensor(out->sensor_online,out->status_valid,out->warn);
    return true;
}

void warehouse_manager_snapshot(warehouse_snapshot_t *s)
{if(!s)return;memset(s,0,sizeof(*s));s->profile=warehouse_manager_profile();s->group_count=laser_profile_group_count(s->profile);for(uint8_t position=1;position<=s->group_count;position++){warehouse_manager_get_position(position,&s->positions[position-1U]);warehouse_position_t *p=&s->positions[position-1U];if(p->config.enabled)s->configured++;if(p->sensor_online)s->online++;if(p->state==WAREHOUSE_STATE_UNKNOWN)s->unknown++;else if(p->state==WAREHOUSE_STATE_EMPTY)s->empty++;else s->occupied++;}}

const char *warehouse_state_name(warehouse_state_t s){return s==WAREHOUSE_STATE_EMPTY?"EMPTY":s==WAREHOUSE_STATE_OCCUPIED?"OCCUPIED":"UNKNOWN";}
const char *warehouse_validation_name(warehouse_validation_t v){switch(v){case WAREHOUSE_INVALID_GROUP:return"INVALID_GROUP";case WAREHOUSE_INVALID_LASER_ID:return"LASER_OUT_OF_GROUP";case WAREHOUSE_DUPLICATE_GROUP:return"DUPLICATE_LASER_GROUP";case WAREHOUSE_GROUP_OFFLINE:return"LASER_GROUP_OFFLINE";case WAREHOUSE_DUPLICATE_LASER_ID:return"DUPLICATE_LASER_ID";case WAREHOUSE_DUPLICATE_CODE:return"DUPLICATE_WAREHOUSE_CODE";case WAREHOUSE_INVALID_DISTANCE:return"INVALID_DISTANCE";case WAREHOUSE_INVALID_TEXT:return"INVALID_TEXT";default:return"VALID";}}
