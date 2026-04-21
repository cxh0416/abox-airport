#ifndef __MQTT_APP_H__
#define __MQTT_APP_H__

#include <stdint.h>

#define MQTT_APP_VERSION_STR "1.0"
#define MQTT_APP_JSON_BUF_SZ 512U
#define MQTT_APP_FIRST_HEARTBEAT_DELAY_MS 3000U
#define MQTT_APP_HEARTBEAT_PERIOD_MS 3000U

void MQTT_App_Init(const char* device_id);
void MQTT_App_Task(void);
void MQTT_App_OnMessage(const char* topic, const char* payload);
const char* MQTT_App_GetCtrlTopic(void);
const char* MQTT_App_GetHeartbeatTopic(void);

#endif
