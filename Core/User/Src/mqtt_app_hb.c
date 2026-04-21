#include <string.h>

#include "cJSON.h"
#include "main.h"
#include "mqtt_app_int.h"
#include "mqtt_core.h"
#include "time_base.h"

extern volatile uint8_t g_mqtt_app_hb_pending;
extern uint32_t g_mqtt_app_last_hb_ok_tick;

void MQTT_App_HandlePendingResponseTask(void)
{
    if (!g_mqtt_app_resp_pending) return;
    if (!MQTT_Core_IsReady()) return;

    if (MQTT_Core_Publish(g_mqtt_app_resp_topic, g_mqtt_app_resp_payload, 0))
    {
        g_mqtt_app_resp_pending = 0;

        if (g_mqtt_app_ota_reset_pending) { g_mqtt_app_ota_reset_tick = HAL_GetTick(); }
    }
}

void MQTT_App_HandleHeartbeatTask(void)
{
    uint32_t now;
    uint64_t ts_ms;
    cJSON* root = 0;
    char json_buf[MQTT_APP_JSON_BUF_SZ];

    now = HAL_GetTick();

    if (!g_mqtt_app_hb_pending)
    {
        if ((g_mqtt_app_seq_no == 1U) && ((now - g_mqtt_app_last_hb_tick) >= MQTT_APP_FIRST_HEARTBEAT_DELAY_MS))
        {
            g_mqtt_app_hb_pending = 1U;
        }
        else if ((g_mqtt_app_seq_no != 1U) && ((now - g_mqtt_app_last_hb_tick) >= MQTT_APP_HEARTBEAT_PERIOD_MS))
        {
            g_mqtt_app_hb_pending = 1U;
        }
    }

    if (!g_mqtt_app_hb_pending) return;
    if (g_mqtt_app_resp_pending) return;
    if (!MQTT_Core_IsReady()) return;

    ts_ms = TimeBase_GetMs();
    memset(json_buf, 0, sizeof(json_buf));

    root = cJSON_CreateObject();
    if (root == 0) return;

    if (cJSON_AddStringToObject(root, "type", "heartbeat") == 0) goto EXIT;
    if (cJSON_AddNumberToObject(root, "seqNo", (double)g_mqtt_app_seq_no) == 0) goto EXIT;
    if (cJSON_AddStringToObject(root, "version", MQTT_APP_VERSION_STR) == 0) goto EXIT;
    if (cJSON_AddNumberToObject(root, "timestamp", (double)ts_ms) == 0) goto EXIT;

    {
        cJSON* data_obj = cJSON_CreateObject();
        if (data_obj == 0) goto EXIT;

        if (MQTT_Core_IsIccidValid())
        {
            const char* iccid = MQTT_Core_GetIccid();
            if (iccid && (iccid[0] != '\0'))
                cJSON_AddStringToObject(data_obj, "iccid", iccid);
            else
                cJSON_AddNullToObject(data_obj, "iccid");
        }
        else
        {
            cJSON_AddNullToObject(data_obj, "iccid");
        }

        cJSON_AddItemToObject(root, "data", data_obj);
    }

    if (cJSON_PrintPreallocated(root, json_buf, sizeof(json_buf), 0))
    {
        if (MQTT_Core_Publish(g_mqtt_app_heartbeat_topic, json_buf, 0))
        {
            g_mqtt_app_hb_pending = 0U;
            g_mqtt_app_last_hb_tick = now;
            g_mqtt_app_last_hb_ok_tick = now;
            g_mqtt_app_seq_no++;
        }
    }

EXIT:
    if (root) cJSON_Delete(root);
}
