#include "mqtt_core.h"

#include <string.h>

#include "main.h"
#include "mqtt_core_int.h"

#define MQTT_CORE_READY_LINE_SILENCE_MS 70000U
#define MQTT_CORE_PUB_OK_TIMEOUT_MS 90000U

MQTT_CoreConfig_t g_mqtt_cfg;
MQTT_CoreRuntime_t g_mqtt_rt;
MQTT_CoreMsgCallback_t g_mqtt_msg_cb = 0;
char g_mqtt_line_buf[MQTT_CORE_RX_LINE_BUF_SZ];
uint16_t g_mqtt_line_len = 0;
volatile uint8_t g_mqtt_last_evt_pending = 0;
char g_mqtt_last_evt_line[128];
char g_mqtt_sub_topic[MQTT_CORE_TOPIC_BUF_SZ];
char g_mqtt_pub_topic[MQTT_CORE_TOPIC_BUF_SZ];
char g_mqtt_pub_pending_topic[MQTT_CORE_TOPIC_BUF_SZ];
char g_mqtt_pub_pending_payload[MQTT_CORE_PAYLOAD_BUF_SZ];
uint8_t g_mqtt_rx_drop_until_lf = 0;
char g_mqtt_iccid[32];
uint8_t g_mqtt_iccid_valid = 0;
uint8_t g_mqtt_iccid_query_pending = 0;
uint32_t g_mqtt_iccid_query_tick = 0;
volatile uint8_t g_mqtt_pub_prompt_pending = 0;
volatile uint8_t g_mqtt_rx_msg_pending = 0;
// volatile uint8_t g_mqtt_need_full_reinit = 0;
char g_mqtt_rx_pending_topic[MQTT_CORE_TOPIC_BUF_SZ];
char g_mqtt_rx_pending_payload[MQTT_CORE_PAYLOAD_BUF_SZ];
uint32_t g_mqtt_last_pub_ok_tick = 0;
uint32_t g_mqtt_last_mqtt_rx_tick = 0;

static uint8_t mqtt_pub_flow_active(void)
{ return (g_mqtt_rt.pub_busy || g_mqtt_rt.pub_wait_prompt || g_mqtt_rt.pub_wait_result) ? 1U : 0U; }

void MQTT_Core_Init(const MQTT_CoreConfig_t* cfg, MQTT_CoreMsgCallback_t cb)
{
    memset(&g_mqtt_cfg, 0, sizeof(g_mqtt_cfg));
    memset(&g_mqtt_rt, 0, sizeof(g_mqtt_rt));
    memset(g_mqtt_line_buf, 0, sizeof(g_mqtt_line_buf));

    g_mqtt_line_len = 0;
    g_mqtt_rx_drop_until_lf = 0;
    g_mqtt_msg_cb = cb;
    g_mqtt_pub_pending_topic[0] = '\0';
    g_mqtt_pub_pending_payload[0] = '\0';
    g_mqtt_iccid[0] = '\0';
    g_mqtt_iccid_valid = 0U;
    g_mqtt_iccid_query_pending = 0U;
    g_mqtt_iccid_query_tick = 0U;
    g_mqtt_pub_prompt_pending = 0U;
    g_mqtt_rx_msg_pending = 0U;
    // g_mqtt_need_full_reinit = 0U;
    g_mqtt_rx_pending_topic[0] = '\0';
    g_mqtt_rx_pending_payload[0] = '\0';
    g_mqtt_last_pub_ok_tick = HAL_GetTick();
    g_mqtt_last_mqtt_rx_tick = HAL_GetTick();

    if (cfg) { g_mqtt_cfg = *cfg; }

    mqtt_copy_topic(g_mqtt_sub_topic, sizeof(g_mqtt_sub_topic), g_mqtt_cfg.sub_topic);
    mqtt_copy_topic(g_mqtt_pub_topic, sizeof(g_mqtt_pub_topic), g_mqtt_cfg.pub_topic);

    g_mqtt_rt.last_rx_tick = HAL_GetTick();
    mqtt_clear_flags();
    mqtt_set_state(MQTT_CORE_ST_IDLE);
}

void MQTT_Core_Reset(void)
{
    memset(&g_mqtt_rt, 0, sizeof(g_mqtt_rt));
    memset(g_mqtt_line_buf, 0, sizeof(g_mqtt_line_buf));

    g_mqtt_line_len = 0;
    g_mqtt_rx_drop_until_lf = 0;
    g_mqtt_pub_pending_topic[0] = '\0';
    g_mqtt_pub_pending_payload[0] = '\0';
    g_mqtt_iccid[0] = '\0';
    g_mqtt_iccid_valid = 0U;
    g_mqtt_iccid_query_pending = 0U;
    g_mqtt_iccid_query_tick = 0U;
    g_mqtt_pub_prompt_pending = 0U;
    g_mqtt_rx_msg_pending = 0U;
    // g_mqtt_need_full_reinit = 0U;
    g_mqtt_rx_pending_topic[0] = '\0';
    g_mqtt_rx_pending_payload[0] = '\0';
    g_mqtt_last_pub_ok_tick = HAL_GetTick();
    g_mqtt_last_mqtt_rx_tick = HAL_GetTick();

    mqtt_copy_topic(g_mqtt_sub_topic, sizeof(g_mqtt_sub_topic), g_mqtt_cfg.sub_topic);
    mqtt_copy_topic(g_mqtt_pub_topic, sizeof(g_mqtt_pub_topic), g_mqtt_cfg.pub_topic);

    g_mqtt_rt.last_rx_tick = HAL_GetTick();
    mqtt_clear_flags();
    mqtt_set_state(MQTT_CORE_ST_IDLE);
}

void MQTT_Core_RequestReconnect(void) { g_mqtt_rt.need_reconnect = 1; }

void MQTT_Core_RequestIccid(void)
{
    if (g_mqtt_rt.state != MQTT_CORE_ST_READY) return;
    if (mqtt_pub_flow_active()) return;
    if (g_mqtt_iccid_query_pending) return;

    g_mqtt_iccid_query_pending = 1U;
    g_mqtt_iccid_query_tick = HAL_GetTick();
    mqtt_send_cmd("AT+QCCID\r\n");
}

uint8_t MQTT_Core_IsIccidValid(void) { return g_mqtt_iccid_valid; }
const char* MQTT_Core_GetIccid(void) { return g_mqtt_iccid_valid ? g_mqtt_iccid : 0; }

void MQTT_Core_SetTopics(const char* sub_topic, const char* pub_topic)
{
    uint8_t changed = 0;

    if (sub_topic)
    {
        if (strcmp(g_mqtt_sub_topic, sub_topic) != 0)
        {
            mqtt_copy_topic(g_mqtt_sub_topic, sizeof(g_mqtt_sub_topic), sub_topic);
            changed = 1;
        }
    }

    if (pub_topic)
    {
        if (strcmp(g_mqtt_pub_topic, pub_topic) != 0)
        {
            mqtt_copy_topic(g_mqtt_pub_topic, sizeof(g_mqtt_pub_topic), pub_topic);
        }
    }

    if (changed)
    {
        if (g_mqtt_rt.state == MQTT_CORE_ST_READY) { g_mqtt_rt.need_resub = 1; }
        else
        {
            g_mqtt_rt.need_reconnect = 1;
        }
    }
}

uint8_t MQTT_Core_IsReady(void) { return (g_mqtt_rt.state == MQTT_CORE_ST_READY) ? 1U : 0U; }
MQTT_CoreState_t MQTT_Core_GetState(void) { return g_mqtt_rt.state; }

uint8_t MQTT_Core_Publish(const char* topic, const char* payload, uint8_t qos)
{
    const char* use_topic;
    uint16_t payload_len;

    if (g_mqtt_rt.state != MQTT_CORE_ST_READY) return 0;
    if (g_mqtt_rt.pub_busy) return 0;
    if (payload == 0) return 0;

    use_topic = topic;
    if ((use_topic == 0) || (use_topic[0] == '\0')) { use_topic = g_mqtt_pub_topic; }
    if ((use_topic == 0) || (use_topic[0] == '\0')) return 0;

    payload_len = (uint16_t)strlen(payload);
    if (payload_len == 0) return 0;
    if (payload_len >= sizeof(g_mqtt_pub_pending_payload)) return 0;

    mqtt_copy_topic(g_mqtt_pub_pending_topic, sizeof(g_mqtt_pub_pending_topic), use_topic);
    strncpy(g_mqtt_pub_pending_payload, payload, sizeof(g_mqtt_pub_pending_payload) - 1U);
    g_mqtt_pub_pending_payload[sizeof(g_mqtt_pub_pending_payload) - 1U] = '\0';

    g_mqtt_rt.pub_busy = 1;
    g_mqtt_rt.pub_wait_prompt = 1;
    g_mqtt_rt.pub_wait_result = 0;
    g_mqtt_rt.pub_tick = HAL_GetTick();
    g_mqtt_pub_prompt_pending = 0U;

    mqtt_send_cmd("AT+QMTPUBEX=%d,0,%d,0,\"%s\",%d\r\n", g_mqtt_cfg.client_idx, qos, g_mqtt_pub_pending_topic,
                  payload_len);
    return 1;
}

void MQTT_Core_Task(void)
{
    uint32_t wait_ms;

    if (g_mqtt_last_evt_pending)
    {
        char evt[128];

        __disable_irq();
        strncpy(evt, g_mqtt_last_evt_line, sizeof(evt) - 1U);
        evt[sizeof(evt) - 1U] = '\0';
        g_mqtt_last_evt_pending = 0U;
        __enable_irq();

        mqtt_dbg_printf("[MQTT] EVT: %s\r\n", evt);
    }

    switch (g_mqtt_rt.state)
    {
        case MQTT_CORE_ST_IDLE:
            g_mqtt_rt.retry_in_state = 0;
            mqtt_set_state(MQTT_CORE_ST_WAIT_BOOT);
            break;

        case MQTT_CORE_ST_WAIT_BOOT:
            if (mqtt_state_timeout(MQTT_CORE_BOOT_WAIT_MS))
            {
                g_mqtt_rt.retry_in_state = 0;
                mqtt_set_state(MQTT_CORE_ST_ECHO_OFF);
            }
            break;

        case MQTT_CORE_ST_ECHO_OFF:
            mqtt_clear_flags();
            mqtt_set_state(MQTT_CORE_ST_WAIT_ECHO_OFF);
            mqtt_send_cmd("ATE0\r\n");
            break;

        case MQTT_CORE_ST_WAIT_ECHO_OFF:
            if (g_mqtt_rt.flag_cfg_ok)
            {
                g_mqtt_rt.retry_in_state = 0;
                mqtt_set_state(MQTT_CORE_ST_CFG);
            }
            else if (g_mqtt_rt.flag_error || mqtt_state_timeout(MQTT_CORE_CFG_TIMEOUT_MS)) { mqtt_fail_current_step(); }
            break;

        case MQTT_CORE_ST_CFG:
            mqtt_clear_flags();
            mqtt_set_state(MQTT_CORE_ST_WAIT_CFG);
            mqtt_send_cmd("AT+QMTCFG=\"recv/mode\",%d,0,0\r\n", g_mqtt_cfg.client_idx);
            break;

        case MQTT_CORE_ST_WAIT_CFG:
            if (g_mqtt_rt.flag_cfg_ok)
            {
                g_mqtt_rt.retry_in_state = 0;
                mqtt_set_state(MQTT_CORE_ST_OPEN);
            }
            else if (g_mqtt_rt.flag_error || mqtt_state_timeout(MQTT_CORE_CFG_TIMEOUT_MS)) { mqtt_fail_current_step(); }
            break;

        case MQTT_CORE_ST_OPEN:
            mqtt_clear_flags();
            mqtt_set_state(MQTT_CORE_ST_WAIT_OPEN);
            mqtt_send_cmd("AT+QMTOPEN=%d,\"%s\",%d\r\n", g_mqtt_cfg.client_idx, g_mqtt_cfg.server_host,
                          g_mqtt_cfg.server_port);
            break;

        case MQTT_CORE_ST_WAIT_OPEN:
            if (g_mqtt_rt.flag_open_ok)
            {
                g_mqtt_rt.connected = 0;
                g_mqtt_rt.need_reconnect = 0U;
                g_mqtt_rt.retry_in_state = 0;
                mqtt_set_state(MQTT_CORE_ST_CONN);
            }
            else if (g_mqtt_rt.flag_error || mqtt_state_timeout(MQTT_CORE_OPEN_TIMEOUT_MS))
            {
                mqtt_fail_current_step();
            }
            break;

        case MQTT_CORE_ST_CONN:
            mqtt_clear_flags();
            mqtt_set_state(MQTT_CORE_ST_WAIT_CONN);
            mqtt_send_cmd("AT+QMTCONN=%d,\"%s\",\"%s\",\"%s\"\r\n", g_mqtt_cfg.client_idx, g_mqtt_cfg.client_id,
                          g_mqtt_cfg.username, g_mqtt_cfg.password);
            break;

        case MQTT_CORE_ST_WAIT_CONN:
            if (g_mqtt_rt.flag_conn_ok)
            {
                g_mqtt_rt.connected = 1;
                g_mqtt_rt.need_reconnect = 0U;
                g_mqtt_rt.retry_in_state = 0;
                mqtt_set_state(MQTT_CORE_ST_SUB);
            }
            else if (g_mqtt_rt.flag_error || mqtt_state_timeout(MQTT_CORE_CONN_TIMEOUT_MS))
            {
                mqtt_fail_current_step();
            }
            break;

        case MQTT_CORE_ST_SUB:
            mqtt_clear_flags();
            mqtt_set_state(MQTT_CORE_ST_WAIT_SUB);
            mqtt_send_cmd("AT+QMTSUB=%d,1,\"%s\",0\r\n", g_mqtt_cfg.client_idx, g_mqtt_sub_topic);
            break;

        case MQTT_CORE_ST_WAIT_SUB:
            if (g_mqtt_rt.flag_sub_ok)
            {
                g_mqtt_rt.subscribed = 1;
                g_mqtt_rt.need_reconnect = 0U;
                g_mqtt_rt.reconnect_count = 0;
                g_mqtt_rt.retry_in_state = 0;
                g_mqtt_rt.last_ready_tick = HAL_GetTick();
                g_mqtt_last_pub_ok_tick = g_mqtt_rt.last_ready_tick;
                g_mqtt_last_mqtt_rx_tick = g_mqtt_rt.last_ready_tick;
                mqtt_set_state(MQTT_CORE_ST_READY);
            }
            else if (g_mqtt_rt.flag_error || mqtt_state_timeout(MQTT_CORE_SUB_TIMEOUT_MS)) { mqtt_fail_current_step(); }
            break;

        case MQTT_CORE_ST_READY:
        {
            static uint32_t last_time_query = 0;
            uint32_t now = HAL_GetTick();

            g_mqtt_rt.need_reconnect = 0U;

            // if (g_mqtt_need_full_reinit)
            // {
            //     g_mqtt_need_full_reinit = 0U;
            //     g_mqtt_rt.retry_in_state = 0U;
            //     mqtt_clear_flags();
            //     mqtt_set_state(MQTT_CORE_ST_WAIT_BOOT);
            //     break;
            // }

            if (g_mqtt_pub_prompt_pending && g_mqtt_rt.pub_wait_prompt)
            {
                g_mqtt_pub_prompt_pending = 0U;
                mqtt_handle_prompt_char();
                break;
            }

            if (g_mqtt_rt.pub_busy && ((now - g_mqtt_rt.pub_tick) > MQTT_CORE_PUB_TIMEOUT_MS))
            {
                g_mqtt_rt.pub_busy = 0;
                g_mqtt_rt.pub_wait_prompt = 0;
                g_mqtt_rt.pub_wait_result = 0;
                mqtt_set_state(MQTT_CORE_ST_RECOVER_PUB);
                break;
            }

            if ((now - g_mqtt_rt.last_rx_tick) > MQTT_CORE_READY_LINE_SILENCE_MS)
            {
                mqtt_dbg_printf("[MQTT] ready silence timeout\r\n");
                mqtt_enter_reconnect_wait();
                break;
            }

            if ((now - g_mqtt_last_pub_ok_tick) > MQTT_CORE_PUB_OK_TIMEOUT_MS)
            {
                mqtt_dbg_printf("[MQTT] no publish success too long\r\n");
                mqtt_enter_reconnect_wait();
                break;
            }

            if (g_mqtt_rx_msg_pending)
            {
                char topic[MQTT_CORE_TOPIC_BUF_SZ];
                char payload[MQTT_CORE_PAYLOAD_BUF_SZ];

                __disable_irq();
                mqtt_copy_topic(topic, sizeof(topic), g_mqtt_rx_pending_topic);
                strncpy(payload, g_mqtt_rx_pending_payload, sizeof(payload) - 1U);
                payload[sizeof(payload) - 1U] = '\0';
                g_mqtt_rx_msg_pending = 0U;
                g_mqtt_rx_pending_topic[0] = '\0';
                g_mqtt_rx_pending_payload[0] = '\0';
                __enable_irq();

                mqtt_dispatch_message(topic, payload);
                break;
            }

            if (mqtt_pub_flow_active())
            {
                if (g_mqtt_rt.flag_closed || g_mqtt_rt.flag_error || g_mqtt_rt.need_reconnect)
                {
                    mqtt_set_state(MQTT_CORE_ST_RECOVER_PUB);
                }
                break;
            }

            if ((last_time_query == 0U) || ((now - last_time_query) > 60000U))
            {
                last_time_query = now;
                mqtt_send_cmd("AT+CCLK?\r\n");
                break;
            }

            if ((!g_mqtt_iccid_valid) && (!g_mqtt_iccid_query_pending))
            {
                MQTT_Core_RequestIccid();
                break;
            }

            if (g_mqtt_iccid_query_pending && ((now - g_mqtt_iccid_query_tick) > 5000U))
            {
                g_mqtt_iccid_query_pending = 0U;
            }

            if (g_mqtt_rt.flag_closed || g_mqtt_rt.flag_error || g_mqtt_rt.need_reconnect)
            {
                mqtt_enter_reconnect_wait();
            }
            else if (g_mqtt_rt.need_resub)
            {
                g_mqtt_rt.need_resub = 0;
                g_mqtt_rt.subscribed = 0;
                g_mqtt_rt.retry_in_state = 0;
                mqtt_set_state(MQTT_CORE_ST_SUB);
            }
        }
        break;

        case MQTT_CORE_ST_RECOVER_PUB:
            if (mqtt_state_timeout(MQTT_CORE_PUB_RECOVER_MS)) { mqtt_enter_reconnect_wait(); }
            break;

        case MQTT_CORE_ST_RECONNECT_WAIT:
            wait_ms = mqtt_calc_reconnect_delay(g_mqtt_rt.reconnect_count);
            if (mqtt_state_timeout(wait_ms))
            {
                mqtt_dbg_printf("[MQTT] reconnect after %lu ms\r\n", wait_ms);
                g_mqtt_rt.retry_in_state = 0;
                mqtt_clear_flags();
                mqtt_set_state(MQTT_CORE_ST_ECHO_OFF);
            }
            break;

        case MQTT_CORE_ST_ERROR:
        default: mqtt_enter_reconnect_wait(); break;
    }
}
