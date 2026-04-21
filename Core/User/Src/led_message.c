#include "led_message.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "serial_bus.h"

/* -------------------- 可按需调整的参数 -------------------- */
#define LED_MSG_ADDR 0x01U
#define LED_MSG_OPCODE_L 0x00U
#define LED_MSG_OPCODE_H 0xD9U

#define LED_MSG_REGION_ID_ASCII '0' /* 固定刷新内码区域0 */
#define LED_MSG_COLOR_ASCII ':'     /* ':'红色 ';'绿色 '<'黄色 '='紫色 '>'蓝色 '?'淡色 '@'白色 */
#define LED_MSG_EFFECT_ASCII '2'    /* '0'自动, '2'连续左移 */

#define LED_MSG_PREFIX_LEN 8U       /* "%disp0:0" */
#define LED_MSG_MAX_TEXT_BYTES 480U /* 预留足够大，避免超过协议限制 */
#define LED_MSG_MAX_FRAME_BYTES (24U + LED_MSG_PREFIX_LEN + LED_MSG_MAX_TEXT_BYTES)

static uint8_t g_led_text[LED_MSG_MAX_TEXT_BYTES];
static uint16_t g_led_text_len = 0;

static uint8_t g_led_active = 0;
static uint8_t g_led_need_clear = 0;
static uint32_t g_led_clear_tick = 0;

/* -------------------- 工具函数 -------------------- */

static int hex_char_to_val(char c)
{
    if ((c >= '0') && (c <= '9')) return (int)(c - '0');
    if ((c >= 'a') && (c <= 'f')) return (int)(c - 'a' + 10);
    if ((c >= 'A') && (c <= 'F')) return (int)(c - 'A' + 10);
    return -1;
}

static int hexstr_to_bytes(const char* hex, uint8_t* out, uint16_t out_max, uint16_t* out_len)
{
    uint16_t n = 0;
    int hi, lo;

    if ((hex == 0) || (out == 0) || (out_len == 0)) return -1;

    while ((*hex != '\0') && isspace((unsigned char)*hex)) { hex++; }

    while (*hex != '\0')
    {
        while ((*hex != '\0') && isspace((unsigned char)*hex)) { hex++; }
        if (*hex == '\0') break;

        hi = hex_char_to_val(*hex++);
        if (hi < 0) return -2;

        while ((*hex != '\0') && isspace((unsigned char)*hex)) { hex++; }
        if (*hex == '\0') return -3; /* 奇数字节 */

        lo = hex_char_to_val(*hex++);
        if (lo < 0) return -4;

        if (n >= out_max) return -5;

        out[n++] = (uint8_t)((hi << 4) | lo);
    }

    *out_len = n;
    return 0;
}

static int parse_time_and_hex(const char* value, uint32_t* time_s, uint8_t* text, uint16_t text_max, uint16_t* text_len)
{
    const char* colon;
    char time_buf[16];
    size_t time_len;
    char* endp = 0;
    unsigned long sec;

    if ((value == 0) || (time_s == 0) || (text == 0) || (text_len == 0)) return -1;

    colon = strchr(value, ':');
    if (colon == 0) return -2;

    time_len = (size_t)(colon - value);
    if ((time_len == 0U) || (time_len >= sizeof(time_buf))) return -3;

    memcpy(time_buf, value, time_len);
    time_buf[time_len] = '\0';

    sec = strtoul(time_buf, &endp, 10);
    if ((endp == 0) || (*endp != '\0')) return -4;

    *time_s = (uint32_t)sec;

    return hexstr_to_bytes(colon + 1, text, text_max, text_len);
}

static int led_send_refresh_raw(const uint8_t* text, uint16_t text_len)
{
    uint8_t frame[LED_MSG_MAX_FRAME_BYTES];
    uint8_t data_len_u8;
    uint16_t data_len;
    uint16_t pos = 0;

    /* 数据区: "%disp0:0" + 文本 */
    data_len = (uint16_t)(LED_MSG_PREFIX_LEN + text_len);
    if (data_len > 512U) return -1;

    data_len_u8 = (uint8_t)data_len;

    /* 帧头 */
    frame[pos++] = 0x55;
    frame[pos++] = 0xAA;
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;

    /* 地址、标志、操作码、保留 */
    frame[pos++] = LED_MSG_ADDR;
    frame[pos++] = 0x01;
    frame[pos++] = LED_MSG_OPCODE_L;
    frame[pos++] = LED_MSG_OPCODE_H;
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;

    /* 帧序号 */
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;

    /* 总长度 32bit 小端 */
    frame[pos++] = (uint8_t)(data_len & 0xFFU);
    frame[pos++] = (uint8_t)((data_len >> 8) & 0xFFU);
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;

    /* 帧长度 16bit 小端 */
    frame[pos++] = (uint8_t)(data_len & 0xFFU);
    frame[pos++] = (uint8_t)((data_len >> 8) & 0xFFU);

    /* 数据区 "%disp0:0" */
    frame[pos++] = 0x25; /* % */
    frame[pos++] = 0x64; /* d */
    frame[pos++] = 0x69; /* i */
    frame[pos++] = 0x73; /* s */
    frame[pos++] = 0x70; /* p */
    frame[pos++] = (uint8_t)LED_MSG_REGION_ID_ASCII;
    frame[pos++] = (uint8_t)LED_MSG_COLOR_ASCII;
    frame[pos++] = (uint8_t)LED_MSG_EFFECT_ASCII;

    /* 文本 */
    if ((text != 0) && (text_len > 0U))
    {
        memcpy(&frame[pos], text, text_len);
        pos = (uint16_t)(pos + text_len);
    }

    /* 帧尾 */
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;
    frame[pos++] = 0x0D;
    frame[pos++] = 0x0A;

    return SerialBus_QueueTx(SERIAL_BUS_DEV_LED, frame, pos) ? 0 : -2;
}

static void led_set_err(char* err, uint16_t err_len, const char* msg)
{
    if ((err != 0) && (err_len > 0U))
    {
        strncpy(err, msg, err_len - 1U);
        err[err_len - 1U] = '\0';
    }
}

/* -------------------- 对外接口 -------------------- */

void LedMessage_Init(void)
{
    g_led_text_len = 0;
    g_led_active = 0;
    g_led_need_clear = 0;
    g_led_clear_tick = 0;
}

int LedMessage_ShowFromValue(const char* value, char* err, uint16_t err_len)
{
    uint32_t time_s = 0;
    uint16_t text_len = 0;
    int ret;

    ret = parse_time_and_hex(value, &time_s, g_led_text, sizeof(g_led_text), &text_len);
    if (ret != 0)
    {
        led_set_err(err, err_len, "show_led_message参数错误");
        return -1;
    }

    if (time_s > 86400U)
    {
        led_set_err(err, err_len, "显示时长过大");
        return -2;
    }

    ret = led_send_refresh_raw(g_led_text, text_len);
    if (ret != 0)
    {
        led_set_err(err, err_len, "LED发送失败");
        return -2;
    }

    g_led_text_len = text_len;
    g_led_active = 1U;

    if (time_s == 0U)
    {
        g_led_need_clear = 0U;
        g_led_clear_tick = 0U;
        led_set_err(err, err_len, "LED常显成功");
    }
    else
    {
        g_led_need_clear = 1U;
        g_led_clear_tick = HAL_GetTick() + (time_s * 1000U);
        led_set_err(err, err_len, "LED显示成功");
    }

    return 0;
}

void LedMessage_Task(void)
{
    uint32_t now;

    if (!g_led_need_clear) return;

    now = HAL_GetTick();
    if ((int32_t)(now - g_led_clear_tick) < 0) return;

    /* 到时清空区域 */
    if (led_send_refresh_raw(0, 0) == 0)
    {
        g_led_need_clear = 0U;
        g_led_active = 0U;
        g_led_text_len = 0U;
    }
}

void LedMessage_OnBusRx(const uint8_t* data, uint16_t len)
{
    (void)data;
    (void)len;
}
