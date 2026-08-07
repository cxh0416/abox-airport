#include <string.h>

#include "main.h"
#include "mqtt_core_int.h"
#include "time_base.h"

extern volatile uint8_t g_mqtt_pub_prompt_pending;
extern volatile uint8_t g_mqtt_rx_msg_pending;
extern char g_mqtt_rx_pending_topic[MQTT_CORE_TOPIC_BUF_SZ];
extern char g_mqtt_rx_pending_payload[MQTT_CORE_PAYLOAD_BUF_SZ];
extern uint32_t g_mqtt_last_pub_ok_tick;
extern uint32_t g_mqtt_last_mqtt_rx_tick;
extern volatile uint8_t g_mqtt_last_evt_pending;
extern char g_mqtt_last_evt_line[128];

static void mqtt_save_last_evt(const char* line)
{
    if (line == 0) return;

    __disable_irq();
    strncpy(g_mqtt_last_evt_line, line, sizeof(g_mqtt_last_evt_line) - 1U);
    g_mqtt_last_evt_line[sizeof(g_mqtt_last_evt_line) - 1U] = '\0';
    g_mqtt_last_evt_pending = 1U;
    __enable_irq();
}

static int mqtt_parse_qccid_line(const char* line, char* iccid, uint16_t size)
{
    const char* p;
    uint16_t n = 0;

    if ((line == 0) || (iccid == 0) || (size == 0)) return 0;

    p = strstr(line, "+QCCID:");
    if (p == 0) return 0;

    p += strlen("+QCCID:");
    while ((*p == ' ') || (*p == '\t')) p++;
    while ((*p >= '0') && (*p <= '9') && (n < (size - 1U))) { iccid[n++] = *p++; }

    iccid[n] = '\0';
    return (n > 0U) ? 1 : 0;
}

static void mqtt_parse_qmtrecv_line(const char* line)
{
    const char* topic_start;
    const char* topic_end;
    const char* payload_start;
    const char* payload_end;
    const char* p;
    int topic_len;
    int payload_len;

    if (line == 0) return;
    if (strstr(line, "+QMTRECV:") == 0) return;

    topic_start = strchr(line, '"');
    if (topic_start == 0) return;
    topic_start++;

    topic_end = strchr(topic_start, '"');
    if (topic_end == 0) return;

    topic_len = (int)(topic_end - topic_start);
    if (topic_len <= 0) return;
    if (topic_len >= (int)MQTT_CORE_TOPIC_BUF_SZ) topic_len = MQTT_CORE_TOPIC_BUF_SZ - 1;

    p = strstr(topic_end, ",\"");
    if (p == 0) return;

    payload_start = p + 2;
    payload_end = strrchr(payload_start, '"');
    if ((payload_end == 0) || (payload_end <= payload_start)) return;

    payload_len = (int)(payload_end - payload_start);
    if (payload_len >= (int)MQTT_CORE_PAYLOAD_BUF_SZ) payload_len = MQTT_CORE_PAYLOAD_BUF_SZ - 1;

    __disable_irq();

    /* 改动点：无论是否 pending，都覆盖成“最新命令” */
    memcpy(g_mqtt_rx_pending_topic, topic_start, (size_t)topic_len);
    g_mqtt_rx_pending_topic[topic_len] = '\0';

    memcpy(g_mqtt_rx_pending_payload, payload_start, (size_t)payload_len);
    g_mqtt_rx_pending_payload[payload_len] = '\0';

    g_mqtt_rx_msg_pending = 1U;
    g_mqtt_last_mqtt_rx_tick = HAL_GetTick();

    __enable_irq();
}

void mqtt_handle_line(const char* line)
{
    if ((line == 0) || (line[0] == '\0')) return;

    g_mqtt_rt.last_rx_tick = HAL_GetTick();
    TimeBase_UpdateFromCCLK(line);
    mqtt_dbg_printf("[MQTT] LINE: %s\r\n", line);

    if (strncmp(line, "AT", 2) == 0) { return; }

    if (strstr(line, "RDY"))
    {
        mqtt_save_last_evt(line);

        /* 上电启动阶段的 RDY 是正常提示，不要污染后面的连接状态 */
        if ((g_mqtt_rt.state == MQTT_CORE_ST_READY) || g_mqtt_rt.connected || g_mqtt_rt.subscribed ||
            g_mqtt_rt.pub_busy || g_mqtt_rt.pub_wait_prompt || g_mqtt_rt.pub_wait_result)
        {
            g_mqtt_rt.flag_closed = 1U;
            g_mqtt_rt.need_reconnect = 1U;
        }
        return;
    }

    if (strncmp(line, "OK", 2) == 0)
    {
        if ((g_mqtt_rt.state == MQTT_CORE_ST_ECHO_OFF) || (g_mqtt_rt.state == MQTT_CORE_ST_WAIT_ECHO_OFF) ||
            (g_mqtt_rt.state == MQTT_CORE_ST_CFG) || (g_mqtt_rt.state == MQTT_CORE_ST_WAIT_CFG))
        {
            g_mqtt_rt.flag_cfg_ok = 1;
        }

        if (g_mqtt_iccid_query_pending) { g_mqtt_iccid_query_pending = 0U; }
        return;
    }

    if (strstr(line, "ERROR") || strstr(line, "+CME ERROR"))
    {
        mqtt_save_last_evt(line);
        g_mqtt_rt.flag_error = 1;
        return;
    }

    if (strstr(line, "+QMTPUBEX:"))
    {
        mqtt_save_last_evt(line);

        if (g_mqtt_rt.pub_busy)
        {
            if (strstr(line, ",0,0"))
            {
                g_mqtt_rt.pub_busy = 0;
                g_mqtt_rt.pub_wait_prompt = 0;
                g_mqtt_rt.pub_wait_result = 0;
                g_mqtt_last_pub_ok_tick = HAL_GetTick();
            }
            else
            {
                g_mqtt_rt.pub_busy = 0;
                g_mqtt_rt.pub_wait_prompt = 0;
                g_mqtt_rt.pub_wait_result = 0;
                g_mqtt_rt.flag_error = 1;
            }
        }
        return;
    }

    if (strstr(line, "+QMTOPEN:"))
    {
        mqtt_save_last_evt(line);

        if (strstr(line, ",0")) { g_mqtt_rt.flag_open_ok = 1; }
        else
        {
            g_mqtt_rt.flag_error = 1;
        }
        return;
    }

    if (strstr(line, "+QMTCONN:"))
    {
        mqtt_save_last_evt(line);

        if (strstr(line, ",0,0")) { g_mqtt_rt.flag_conn_ok = 1; }
        else
        {
            g_mqtt_rt.flag_error = 1;
        }
        return;
    }

    if (strstr(line, "+QMTSUB:"))
    {
        mqtt_save_last_evt(line);

        if (strstr(line, ",1,0") || strstr(line, ",1,1") || strstr(line, ",0,0")) { g_mqtt_rt.flag_sub_ok = 1; }
        else
        {
            g_mqtt_rt.flag_error = 1;
        }
        return;
    }

    if (strstr(line, "+QMTSTAT:") || strstr(line, "+QMTCLOSE:") || strstr(line, "CLOSED"))
    {
        mqtt_save_last_evt(line);
        g_mqtt_rt.flag_closed = 1;
        return;
    }

    if (strstr(line, "+QCCID:"))
    {
        char iccid_buf[32];

        mqtt_save_last_evt(line);

        if (mqtt_parse_qccid_line(line, iccid_buf, sizeof(iccid_buf)))
        {
            strncpy(g_mqtt_iccid, iccid_buf, sizeof(g_mqtt_iccid) - 1U);
            g_mqtt_iccid[sizeof(g_mqtt_iccid) - 1U] = '\0';
            g_mqtt_iccid_valid = 1U;
            g_mqtt_iccid_query_pending = 0U;
        }
        return;
    }

    if (strstr(line, "+QMTRECV:"))
    {
        mqtt_save_last_evt(line);
        mqtt_parse_qmtrecv_line(line);
        return;
    }

    if ((line[0] == '{') || (line[0] == '[')) { return; }
}

void MQTT_Core_OnUartRx(uint8_t* data, uint16_t len)
{
    uint16_t i;
    char ch;

    if ((data == 0) || (len == 0)) return;

    for (i = 0; i < len; i++)
    {
        ch = (char)data[i];

        if (ch == '>')
        {
            g_mqtt_pub_prompt_pending = 1U;
            continue;
        }

        if (g_mqtt_rx_drop_until_lf)
        {
            if (ch == '\n')
            {
                g_mqtt_rx_drop_until_lf = 0;
                g_mqtt_line_len = 0;
            }
            continue;
        }

        if (ch == '\r') { continue; }
        else if (ch == '\n')
        {
            if (g_mqtt_line_len > 0)
            {
                g_mqtt_line_buf[g_mqtt_line_len] = '\0';
                mqtt_handle_line(g_mqtt_line_buf);
                g_mqtt_line_len = 0;
            }
        }
        else
        {
            if (g_mqtt_line_len < (MQTT_CORE_RX_LINE_BUF_SZ - 1U)) { g_mqtt_line_buf[g_mqtt_line_len++] = ch; }
            else
            {
                g_mqtt_rx_drop_until_lf = 1;
                g_mqtt_line_len = 0;
            }
        }
    }
}
