#include <stdio.h>
#include <string.h>

#include "app_rc.h"
#include "cJSON.h"
#include "led_message.h"
#include "main.h"
#include "motor485.h"
#include "mqtt_app_int.h"
#include "ota_info.h"
#include "time_base.h"

void MQTT_App_SetPendingResponse(const char* topic, const char* payload)
{
    if ((topic == 0) || (payload == 0)) return;

    strncpy(g_mqtt_app_resp_topic, topic, sizeof(g_mqtt_app_resp_topic) - 1U);
    g_mqtt_app_resp_topic[sizeof(g_mqtt_app_resp_topic) - 1U] = '\0';

    strncpy(g_mqtt_app_resp_payload, payload, sizeof(g_mqtt_app_resp_payload) - 1U);
    g_mqtt_app_resp_payload[sizeof(g_mqtt_app_resp_payload) - 1U] = '\0';

    g_mqtt_app_resp_pending = 1;
}

void MQTT_App_HandleCommandMessage(const char* topic, const char* payload)
{
    cJSON* root = 0;
    cJSON* type = 0;
    cJSON* seqNo = 0;
    cJSON* data = 0;
    cJSON* command = 0;
    cJSON* value = 0;

    const char* type_str = 0;
    const char* cmd_str = 0;
    const char* val_str = 0;

    uint32_t req_seq = 0;
    int on = 0;
    int motor_val = 0;
    int code = 200;
    char msg[64] = {0};

    (void)topic;

    if (payload == 0) return;

    /* OTA 待复位期间，不再处理新的业务消息 */
    if (g_mqtt_app_ota_reset_pending) return;

    root = cJSON_Parse(payload);
    if (root == 0) return;

    type = cJSON_GetObjectItem(root, "type");
    seqNo = cJSON_GetObjectItem(root, "seqNo");
    data = cJSON_GetObjectItem(root, "data");

    if (!cJSON_IsString(type) || !cJSON_IsNumber(seqNo) || !cJSON_IsObject(data))
    {
        cJSON_Delete(root);
        return;
    }

    type_str = type->valuestring;
    req_seq = (uint32_t)(seqNo->valuedouble);
    command = cJSON_GetObjectItem(data, "command");
    value = cJSON_GetObjectItem(data, "value");

    if (!cJSON_IsString(command))
    {
        code = 400;
        snprintf(msg, sizeof(msg), "command错误");
        goto RESPONSE;
    }

    cmd_str = command->valuestring;
    val_str = cJSON_IsString(value) ? value->valuestring : 0;

    if (strcmp(cmd_str, "show_led_message") == 0)
    {
        if (LedMessage_ShowFromValue(val_str, msg, sizeof(msg)) != 0) { code = 400; }
        else
        {
            code = 200;
            if (msg[0] == '\0') { snprintf(msg, sizeof(msg), "ok"); }
        }
        goto RESPONSE;
    }

    if (strcmp(cmd_str, "ota_upgrade") == 0)
    {
        if (!cJSON_IsString(value) || (val_str == 0) || (val_str[0] == '\0'))
        {
            code = 400;
            snprintf(msg, sizeof(msg), "value必须是升级URL");
            goto RESPONSE;
        }

        if (strlen(val_str) >= 128U)
        {
            code = 400;
            snprintf(msg, sizeof(msg), "url过长");
            goto RESPONSE;
        }

        OTA_Info_SetRequest(0, 0, val_str);

        g_mqtt_app_ota_reset_pending = 1;
        g_mqtt_app_ota_reset_tick = 0;
        g_mqtt_app_ota_request_tick = HAL_GetTick();

        snprintf(msg, sizeof(msg), "ota request saved");
        goto RESPONSE;
    }

    if ((strcmp(cmd_str, "fence_motor_left") == 0) || (strcmp(cmd_str, "fence_motor_right") == 0) ||
        (strcmp(cmd_str, "fence_motor_tail") == 0))
    {
        uint8_t slave = 0;
        MotorCmd_t cmd;

        if (!cJSON_IsNumber(value))
        {
            code = 400;
            snprintf(msg, sizeof(msg), "value必须是数字");
            goto RESPONSE;
        }

        motor_val = (int)(value->valuedouble);

        if (motor_val == -1)
            cmd = MOTOR_CMD_REV;
        else if (motor_val == 0)
            cmd = MOTOR_CMD_STOP;
        else if (motor_val == 1)
            cmd = MOTOR_CMD_FWD;
        else
        {
            code = 400;
            snprintf(msg, sizeof(msg), "value仅支持-1/0/1");
            goto RESPONSE;
        }

        if (strcmp(cmd_str, "fence_motor_left") == 0)
            slave = 1U;
        else if (strcmp(cmd_str, "fence_motor_right") == 0)
            slave = 2U;
        else if (strcmp(cmd_str, "fence_motor_tail") == 0)
            slave = 3U;

        if (AppRc_IsMotorCtrlActive())
        {
            code = 403;
            snprintf(msg, sizeof(msg), "遥控控制中，MQTT电机命令已忽略");
            goto RESPONSE;
        }

        Motor485_RequestSet(slave, cmd);
        snprintf(msg, sizeof(msg), "%s=%d ok", cmd_str, motor_val);
        goto RESPONSE;
    }

    if (!cJSON_IsString(value) || (val_str == 0))
    {
        code = 400;
        snprintf(msg, sizeof(msg), "value必须是turn_on/turn_off");
        goto RESPONSE;
    }

    if (strcmp(val_str, "turn_on") == 0) { on = 1; }
    else if (strcmp(val_str, "turn_off") == 0) { on = 0; }
    else
    {
        code = 400;
        snprintf(msg, sizeof(msg), "value错误");
        goto RESPONSE;
    }

    if (strcmp(cmd_str, "laser_light") == 0)
    {
        HAL_GPIO_WritePin(PVIN_OUT1_GPIO_Port, PVIN_OUT1_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
        snprintf(msg, sizeof(msg), "laser %s", on ? "ON" : "OFF");
    }
    else if (strcmp(cmd_str, "flash_light") == 0)
    {
        HAL_GPIO_WritePin(PVIN_OUT2_GPIO_Port, PVIN_OUT2_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
        snprintf(msg, sizeof(msg), "flash %s", on ? "ON" : "OFF");
    }
    else if (strcmp(cmd_str, "blue_light") == 0)
    {
        HAL_GPIO_WritePin(PVIN_OUT3_GPIO_Port, PVIN_OUT3_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
        snprintf(msg, sizeof(msg), "blue %s", on ? "ON" : "OFF");
    }
    else
    {
        code = 400;
        snprintf(msg, sizeof(msg), "未知指令");
    }

RESPONSE:
    if (strcmp(type_str, "noReply") == 0)
    {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type_str, "request") == 0)
    {
        cJSON* resp = 0;
        cJSON* resp_data = 0;
        char buf[MQTT_APP_JSON_BUF_SZ] = {0};

        resp = cJSON_CreateObject();
        resp_data = cJSON_CreateObject();

        if ((resp != 0) && (resp_data != 0))
        {
            cJSON_AddStringToObject(resp, "type", "response");
            cJSON_AddNumberToObject(resp, "seqNo", (double)req_seq);
            cJSON_AddStringToObject(resp, "version", MQTT_APP_VERSION_STR);
            cJSON_AddNumberToObject(resp, "timestamp", (double)TimeBase_GetMs());

            cJSON_AddItemToObject(resp, "data", resp_data);
            cJSON_AddNumberToObject(resp_data, "code", code);
            cJSON_AddStringToObject(resp_data, "msg", msg);

            if (cJSON_PrintPreallocated(resp, buf, sizeof(buf), 0))
            {
                MQTT_App_SetPendingResponse(g_mqtt_app_ctrl_topic, buf);
            }
        }

        if (resp) { cJSON_Delete(resp); }
    }

    cJSON_Delete(root);
}