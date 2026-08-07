#ifndef __MQTT_APP_INT_H__
#define __MQTT_APP_INT_H__

#include <stdint.h>

#include "mqtt_app.h"

extern char g_mqtt_app_device_id[64];
extern char g_mqtt_app_heartbeat_topic[128];
extern char g_mqtt_app_ctrl_topic[128];
extern uint32_t g_mqtt_app_last_hb_tick;
extern uint32_t g_mqtt_app_last_hb_ok_tick;
extern uint32_t g_mqtt_app_seq_no;
extern volatile uint8_t g_mqtt_app_hb_pending;
extern volatile uint8_t g_mqtt_app_resp_pending;
extern char g_mqtt_app_resp_topic[128];
extern char g_mqtt_app_resp_payload[MQTT_APP_JSON_BUF_SZ];

extern volatile uint8_t g_mqtt_app_ota_reset_pending;
extern uint32_t g_mqtt_app_ota_reset_tick;
extern uint32_t g_mqtt_app_ota_request_tick;

void MQTT_App_SetPendingResponse(const char* topic, const char* payload);
void MQTT_App_HandleCommandMessage(const char* topic, const char* payload);
void MQTT_App_HandlePendingResponseTask(void);
void MQTT_App_HandleHeartbeatTask(void);

#endif