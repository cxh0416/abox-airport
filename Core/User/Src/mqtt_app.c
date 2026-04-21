#include "mqtt_app.h"

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "mqtt_app_int.h"
#include "mqtt_core.h"

#define MQTT_APP_OTA_RESET_DELAY_MS 200U
#define MQTT_APP_OTA_FORCE_RESET_MS 3000U

char g_mqtt_app_device_id[64];
char g_mqtt_app_heartbeat_topic[128];
char g_mqtt_app_ctrl_topic[128];
uint32_t g_mqtt_app_last_hb_tick = 0;
uint32_t g_mqtt_app_last_hb_ok_tick = 0;
uint32_t g_mqtt_app_seq_no = 1;
volatile uint8_t g_mqtt_app_hb_pending = 0;
volatile uint8_t g_mqtt_app_resp_pending = 0;
char g_mqtt_app_resp_topic[128];
char g_mqtt_app_resp_payload[MQTT_APP_JSON_BUF_SZ];

volatile uint8_t g_mqtt_app_ota_reset_pending = 0;
uint32_t g_mqtt_app_ota_reset_tick = 0;
uint32_t g_mqtt_app_ota_request_tick = 0;

void MQTT_App_Init(const char* device_id)
{
    if (device_id == 0)
    {
        g_mqtt_app_device_id[0] = '\0';
        g_mqtt_app_heartbeat_topic[0] = '\0';
        g_mqtt_app_ctrl_topic[0] = '\0';
        g_mqtt_app_resp_topic[0] = '\0';
        g_mqtt_app_resp_payload[0] = '\0';
        g_mqtt_app_hb_pending = 0;
        g_mqtt_app_resp_pending = 0;
        g_mqtt_app_ota_reset_pending = 0;
        g_mqtt_app_ota_reset_tick = 0;
        g_mqtt_app_ota_request_tick = 0;
        return;
    }

    strncpy(g_mqtt_app_device_id, device_id, sizeof(g_mqtt_app_device_id) - 1U);
    g_mqtt_app_device_id[sizeof(g_mqtt_app_device_id) - 1U] = '\0';

    snprintf(g_mqtt_app_heartbeat_topic, sizeof(g_mqtt_app_heartbeat_topic), "/zxwl/vehicle/%s/heartbeat",
             g_mqtt_app_device_id);
    snprintf(g_mqtt_app_ctrl_topic, sizeof(g_mqtt_app_ctrl_topic), "/zxwl/vehicle/%s/ctrl", g_mqtt_app_device_id);

    g_mqtt_app_last_hb_tick = HAL_GetTick();
    g_mqtt_app_last_hb_ok_tick = g_mqtt_app_last_hb_tick;
    g_mqtt_app_seq_no = 1;
    g_mqtt_app_hb_pending = 0;

    g_mqtt_app_resp_topic[0] = '\0';
    g_mqtt_app_resp_payload[0] = '\0';
    g_mqtt_app_resp_pending = 0;

    g_mqtt_app_ota_reset_pending = 0;
    g_mqtt_app_ota_reset_tick = 0;
    g_mqtt_app_ota_request_tick = 0;
}

void MQTT_App_Task(void)
{
    uint32_t now = HAL_GetTick();

    if (g_mqtt_app_ota_reset_pending)
    {
        /*
         * 已进入 OTA 复位流程后：
         * 1) 不再发心跳
         * 2) 不再跑普通业务
         * 3) 只允许等待“响应发完”或“超时强制复位”
         */

        if (g_mqtt_app_ota_reset_tick != 0U)
        {
            if ((now - g_mqtt_app_ota_reset_tick) >= MQTT_APP_OTA_RESET_DELAY_MS) { NVIC_SystemReset(); }
            return;
        }

        /* 还没拿到响应发送成功，只尝试发送响应，不做 heartbeat */
        if (MQTT_Core_IsReady() && g_mqtt_app_resp_pending) { MQTT_App_HandlePendingResponseTask(); }

        /* 响应一直发不出去，也不能无限卡住，直接强制复位 */
        if ((g_mqtt_app_ota_request_tick != 0U) && ((now - g_mqtt_app_ota_request_tick) >= MQTT_APP_OTA_FORCE_RESET_MS))
        {
            NVIC_SystemReset();
        }

        return;
    }

    if (MQTT_Core_IsReady() && g_mqtt_app_resp_pending) { MQTT_App_HandlePendingResponseTask(); }

    MQTT_App_HandleHeartbeatTask();
}

void MQTT_App_OnMessage(const char* topic, const char* payload)
{
    if (g_mqtt_app_ota_reset_pending) return;
    MQTT_App_HandleCommandMessage(topic, payload);
}

const char* MQTT_App_GetCtrlTopic(void) { return g_mqtt_app_ctrl_topic; }
const char* MQTT_App_GetHeartbeatTopic(void) { return g_mqtt_app_heartbeat_topic; }