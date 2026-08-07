#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "mqtt_core_int.h"
#include "usart.h"

#define MQTT_EC_UART huart1
#define MQTT_DBG_UART huart5

void mqtt_dbg_printf(const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;

    if (__get_IPSR() != 0U) return;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0) return;
    if (n >= (int)sizeof(buf))
    {
        n = sizeof(buf) - 1;
        buf[n] = '\0';
    }

    // HAL_UART_Transmit(&MQTT_DBG_UART, (uint8_t*)buf, (uint16_t)n, 100);
}

void mqtt_uart_send_raw(const uint8_t* data, uint16_t len)
{
    if ((data == 0) || (len == 0)) return;
    HAL_UART_Transmit(&MQTT_EC_UART, (uint8_t*)data, len, 1000);
}

void mqtt_send_cmd(const char* fmt, ...)
{
    char buf[MQTT_CORE_CMD_BUF_SZ];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0) return;
    if (n >= (int)sizeof(buf))
    {
        n = sizeof(buf) - 1;
        buf[n] = '\0';
    }

    mqtt_uart_send_raw((const uint8_t*)buf, (uint16_t)n);
    mqtt_dbg_printf("[MQTT] TX: %s", buf);
}

void mqtt_clear_flags(void)
{
    g_mqtt_rt.flag_cfg_ok = 0;
    g_mqtt_rt.flag_open_ok = 0;
    g_mqtt_rt.flag_conn_ok = 0;
    g_mqtt_rt.flag_sub_ok = 0;
    g_mqtt_rt.flag_closed = 0;
    g_mqtt_rt.flag_error = 0;
}

void mqtt_set_state(MQTT_CoreState_t st)
{
    g_mqtt_rt.state = st;
    g_mqtt_rt.state_tick = HAL_GetTick();
    mqtt_dbg_printf("[MQTT] state -> %d\r\n", (int)st);
}

uint8_t mqtt_state_timeout(uint32_t timeout_ms)
{ return ((HAL_GetTick() - g_mqtt_rt.state_tick) >= timeout_ms) ? 1U : 0U; }

uint32_t mqtt_calc_reconnect_delay(uint8_t reconnect_count)
{
    uint32_t delay = MQTT_CORE_RECONNECT_DELAY_MIN_MS;

    while (reconnect_count > 0U)
    {
        if (delay >= (MQTT_CORE_RECONNECT_DELAY_MAX_MS / 2U))
        {
            delay = MQTT_CORE_RECONNECT_DELAY_MAX_MS;
            break;
        }
        delay *= 2U;
        reconnect_count--;
    }

    if (delay > MQTT_CORE_RECONNECT_DELAY_MAX_MS) { delay = MQTT_CORE_RECONNECT_DELAY_MAX_MS; }
    return delay;
}

void mqtt_copy_topic(char* dst, uint16_t dst_sz, const char* src)
{
    if ((dst == 0) || (dst_sz == 0)) return;
    if (src == 0)
    {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_sz - 1U);
    dst[dst_sz - 1U] = '\0';
}

void mqtt_dispatch_message(const char* topic, const char* payload)
{
    if (g_mqtt_msg_cb) { g_mqtt_msg_cb(topic, payload); }
}

void mqtt_enter_reconnect_wait(void)
{
    uint8_t need_close = 0U;

    if (g_mqtt_rt.connected || g_mqtt_rt.subscribed || g_mqtt_rt.pub_busy || g_mqtt_rt.pub_wait_prompt ||
        g_mqtt_rt.pub_wait_result)
    {
        need_close = 1U;
    }

    g_mqtt_rt.connected = 0;
    g_mqtt_rt.subscribed = 0;
    g_mqtt_rt.need_reconnect = 0;

    g_mqtt_rt.pub_busy = 0;
    g_mqtt_rt.pub_wait_prompt = 0;
    g_mqtt_rt.pub_wait_result = 0;
    g_mqtt_line_len = 0;
    g_mqtt_rx_drop_until_lf = 0;

    if (need_close) { mqtt_send_cmd("AT+QMTCLOSE=%d\r\n", g_mqtt_cfg.client_idx); }

    if (g_mqtt_rt.reconnect_count < 250U) { g_mqtt_rt.reconnect_count++; }

    mqtt_clear_flags();
    mqtt_set_state(MQTT_CORE_ST_RECONNECT_WAIT);
}

void mqtt_fail_current_step(void)
{
    g_mqtt_rt.connected = 0;
    g_mqtt_rt.subscribed = 0;

    if (g_mqtt_rt.retry_in_state < MQTT_CORE_MAX_STATE_RETRY)
    {
        g_mqtt_rt.retry_in_state++;
        mqtt_dbg_printf("[MQTT] retry_in_state=%d\r\n", (int)g_mqtt_rt.retry_in_state);

        switch (g_mqtt_rt.state)
        {
            case MQTT_CORE_ST_WAIT_ECHO_OFF: mqtt_set_state(MQTT_CORE_ST_ECHO_OFF); break;
            case MQTT_CORE_ST_WAIT_CFG: mqtt_set_state(MQTT_CORE_ST_CFG); break;
            case MQTT_CORE_ST_WAIT_OPEN: mqtt_set_state(MQTT_CORE_ST_OPEN); break;
            case MQTT_CORE_ST_WAIT_CONN: mqtt_set_state(MQTT_CORE_ST_CONN); break;
            case MQTT_CORE_ST_WAIT_SUB: mqtt_set_state(MQTT_CORE_ST_SUB); break;
            default: mqtt_enter_reconnect_wait(); break;
        }
    }
    else
    {
        mqtt_enter_reconnect_wait();
    }
}

void mqtt_handle_prompt_char(void)
{
    uint16_t payload_len;

    if ((!g_mqtt_rt.pub_busy) || (!g_mqtt_rt.pub_wait_prompt))
    {
        mqtt_dbg_printf("[MQTT] prompt ignored\r\n");
        return;
    }

    payload_len = (uint16_t)strlen(g_mqtt_pub_pending_payload);
    if (payload_len == 0U)
    {
        mqtt_dbg_printf("[MQTT] payload empty\r\n");
        g_mqtt_rt.pub_busy = 0;
        g_mqtt_rt.pub_wait_prompt = 0;
        g_mqtt_rt.pub_wait_result = 0;
        g_mqtt_rt.flag_error = 1;
        return;
    }

    mqtt_dbg_printf("[MQTT] PUB payload len=%u\r\n", payload_len);
    mqtt_dbg_printf("[MQTT] PUB payload: %s\r\n", g_mqtt_pub_pending_payload);

    mqtt_uart_send_raw((const uint8_t*)g_mqtt_pub_pending_payload, payload_len);

    {
        uint8_t ctrl_z = 0x1A;
        mqtt_uart_send_raw(&ctrl_z, 1);
    }

    mqtt_dbg_printf("[MQTT] payload+CTRL_Z sent\r\n");

    g_mqtt_rt.pub_wait_prompt = 0;
    g_mqtt_rt.pub_wait_result = 1;
    g_mqtt_rt.pub_tick = HAL_GetTick();
}
