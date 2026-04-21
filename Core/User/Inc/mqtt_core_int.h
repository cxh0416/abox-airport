#ifndef __MQTT_CORE_INT_H__
#define __MQTT_CORE_INT_H__

#include <stdint.h>

#include "mqtt_core.h"

#define MQTT_CORE_CMD_BUF_SZ 256U
#define MQTT_CORE_RX_LINE_BUF_SZ 512U
#define MQTT_CORE_TOPIC_BUF_SZ 128U
#define MQTT_CORE_PAYLOAD_BUF_SZ 512U

#define MQTT_CORE_BOOT_WAIT_MS 5000U
#define MQTT_CORE_CFG_TIMEOUT_MS 3000U
#define MQTT_CORE_OPEN_TIMEOUT_MS 10000U
#define MQTT_CORE_CONN_TIMEOUT_MS 10000U
#define MQTT_CORE_SUB_TIMEOUT_MS 5000U
#define MQTT_CORE_PUB_TIMEOUT_MS 5000U
#define MQTT_CORE_PUB_RECOVER_MS 300U
#define MQTT_CORE_RECONNECT_DELAY_MIN_MS 4000U
#define MQTT_CORE_RECONNECT_DELAY_MAX_MS 60000U
#define MQTT_CORE_MAX_STATE_RETRY 3U

extern MQTT_CoreConfig_t g_mqtt_cfg;
extern MQTT_CoreRuntime_t g_mqtt_rt;
extern MQTT_CoreMsgCallback_t g_mqtt_msg_cb;
extern char g_mqtt_line_buf[MQTT_CORE_RX_LINE_BUF_SZ];
extern uint16_t g_mqtt_line_len;
extern char g_mqtt_sub_topic[MQTT_CORE_TOPIC_BUF_SZ];
extern char g_mqtt_pub_topic[MQTT_CORE_TOPIC_BUF_SZ];
extern char g_mqtt_pub_pending_topic[MQTT_CORE_TOPIC_BUF_SZ];
extern char g_mqtt_pub_pending_payload[MQTT_CORE_PAYLOAD_BUF_SZ];
extern uint8_t g_mqtt_rx_drop_until_lf;
extern char g_mqtt_iccid[32];
extern uint8_t g_mqtt_iccid_valid;
extern uint8_t g_mqtt_iccid_query_pending;
extern uint32_t g_mqtt_iccid_query_tick;
extern volatile uint8_t g_mqtt_pub_prompt_pending;
extern volatile uint8_t g_mqtt_rx_msg_pending;
// extern volatile uint8_t g_mqtt_need_full_reinit;
extern char g_mqtt_rx_pending_topic[MQTT_CORE_TOPIC_BUF_SZ];
extern char g_mqtt_rx_pending_payload[MQTT_CORE_PAYLOAD_BUF_SZ];
extern uint32_t g_mqtt_last_pub_ok_tick;
extern uint32_t g_mqtt_last_mqtt_rx_tick;
extern volatile uint8_t g_mqtt_last_evt_pending;
extern char g_mqtt_last_evt_line[128];

void mqtt_dbg_printf(const char* fmt, ...);
void mqtt_uart_send_raw(const uint8_t* data, uint16_t len);
void mqtt_send_cmd(const char* fmt, ...);
void mqtt_clear_flags(void);
void mqtt_set_state(MQTT_CoreState_t st);
uint8_t mqtt_state_timeout(uint32_t timeout_ms);
uint32_t mqtt_calc_reconnect_delay(uint8_t reconnect_count);
void mqtt_copy_topic(char* dst, uint16_t dst_sz, const char* src);
void mqtt_dispatch_message(const char* topic, const char* payload);
void mqtt_enter_reconnect_wait(void);
void mqtt_fail_current_step(void);
void mqtt_handle_prompt_char(void);
void mqtt_handle_line(const char* line);

#endif
