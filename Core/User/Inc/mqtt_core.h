#ifndef __MQTT_CORE_H__
#define __MQTT_CORE_H__

#include <stdint.h>

typedef enum
{
    MQTT_CORE_ST_IDLE = 0,
    MQTT_CORE_ST_WAIT_BOOT,
    MQTT_CORE_ST_ECHO_OFF,
    MQTT_CORE_ST_WAIT_ECHO_OFF,
    MQTT_CORE_ST_CFG,
    MQTT_CORE_ST_WAIT_CFG,
    MQTT_CORE_ST_OPEN,
    MQTT_CORE_ST_WAIT_OPEN,
    MQTT_CORE_ST_CONN,
    MQTT_CORE_ST_WAIT_CONN,
    MQTT_CORE_ST_SUB,
    MQTT_CORE_ST_WAIT_SUB,
    MQTT_CORE_ST_READY,
    MQTT_CORE_ST_RECOVER_PUB,
    MQTT_CORE_ST_RECONNECT_WAIT,
    MQTT_CORE_ST_ERROR
} MQTT_CoreState_t;

typedef struct
{
    uint8_t client_idx;
    char server_host[64];
    uint16_t server_port;
    char client_id[64];
    char username[32];
    char password[32];
    char sub_topic[128];
    char pub_topic[128];
} MQTT_CoreConfig_t;

typedef void (*MQTT_CoreMsgCallback_t)(const char* topic, const char* payload);

typedef struct
{
    MQTT_CoreState_t state;
    uint32_t state_tick;
    uint32_t last_rx_tick;
    uint32_t last_ready_tick;
    uint32_t pub_tick;
    uint8_t connected;
    uint8_t subscribed;
    uint8_t need_reconnect;
    uint8_t need_resub;
    uint8_t retry_in_state;
    uint8_t reconnect_count;
    uint8_t flag_cfg_ok;
    uint8_t flag_open_ok;
    uint8_t flag_conn_ok;
    uint8_t flag_sub_ok;
    uint8_t flag_closed;
    uint8_t flag_error;
    uint8_t pub_busy;
    uint8_t pub_wait_prompt;
    uint8_t pub_wait_result;
} MQTT_CoreRuntime_t;

void MQTT_Core_Init(const MQTT_CoreConfig_t* cfg, MQTT_CoreMsgCallback_t cb);
void MQTT_Core_Reset(void);
void MQTT_Core_Task(void);
void MQTT_Core_OnUartRx(uint8_t* data, uint16_t len);
void MQTT_Core_RequestReconnect(void);
void MQTT_Core_RequestIccid(void);
void MQTT_Core_SetTopics(const char* sub_topic, const char* pub_topic);
uint8_t MQTT_Core_IsReady(void);
MQTT_CoreState_t MQTT_Core_GetState(void);
uint8_t MQTT_Core_Publish(const char* topic, const char* payload, uint8_t qos);
uint8_t MQTT_Core_IsIccidValid(void);
const char* MQTT_Core_GetIccid(void);

#endif
