#include "ota_http.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_cfg.h"
#include "boot_dbg.h"
#include "main.h"
#include "ota_ec_at.h"
#include "ota_flash.h"
#include "usart.h"

#define OTA_HTTP_URL_MAX_LEN 192
#define OTA_HTTP_TIMEOUT_MS 40000U
#define OTA_HTTP_GET_TIMEOUT_MS 30000U
#define OTA_HTTP_READ_TIMEOUT_MS 60000U
#define OTA_HTTP_RAW_BUF_SZ 16384U
#define OTA_HTTP_READ_DONE_GRACE_MS 200U

typedef enum
{
    OTA_HTTP_ST_IDLE = 0,

    OTA_HTTP_ST_SEND_QICSGP,
    OTA_HTTP_ST_WAIT_QICSGP,

    OTA_HTTP_ST_SEND_QIACT,
    OTA_HTTP_ST_WAIT_QIACT_OK,

    OTA_HTTP_ST_SEND_QIACT_QUERY,
    OTA_HTTP_ST_WAIT_QIACT_QUERY,

    OTA_HTTP_ST_SEND_CFG_CTX,
    OTA_HTTP_ST_WAIT_CFG_CTX,

    OTA_HTTP_ST_SEND_CFG_REQHDR,
    OTA_HTTP_ST_WAIT_CFG_REQHDR,

    OTA_HTTP_ST_SEND_CFG_RSPHDR,
    OTA_HTTP_ST_WAIT_CFG_RSPHDR,

    OTA_HTTP_ST_SEND_URL_CMD,
    OTA_HTTP_ST_WAIT_URL_CONNECT,
    OTA_HTTP_ST_SEND_URL_DATA,
    OTA_HTTP_ST_WAIT_URL_OK,

    OTA_HTTP_ST_SEND_HTTPGET,
    OTA_HTTP_ST_WAIT_HTTPGET,

    OTA_HTTP_ST_SEND_HTTPREAD,
    OTA_HTTP_ST_WAIT_HTTPREAD_CONNECT,
    OTA_HTTP_ST_WAIT_HTTPREAD_DONE,

    OTA_HTTP_ST_DONE,
    OTA_HTTP_ST_ERROR
} OTA_HttpState_t;

typedef struct
{
    OTA_HttpState_t state;
    uint32_t ts;                    /* 命令/阶段超时戳，不再给 raw 收包共用 */
    volatile uint32_t last_rx_tick; /* 仅用于 raw 收包保活超时 */

    char url[OTA_HTTP_URL_MAX_LEN];

    volatile uint8_t busy;
    volatile uint8_t done;
    volatile uint8_t error;

    volatile uint8_t ok_flag;
    volatile uint8_t connect_flag;
    volatile uint8_t qiact_ok;
    volatile uint8_t qiact_query_done;
    volatile uint8_t qiact_ip_ok;
    volatile uint8_t get_ok;
    volatile uint8_t read_ok;

    int get_err;
    int http_code;
    int content_len;

    uint8_t flash_started;
    uint32_t expected_written_len;

    uint8_t raw_buf[OTA_HTTP_RAW_BUF_SZ];
    volatile uint16_t raw_len;

    volatile uint32_t recv_len;
    volatile uint8_t flash_fail;

    /* 只用于 read_ok 后的短暂宽限，避免最后 1 字节尚未计入就提前收尾 */
    volatile uint8_t read_done_seen;
    volatile uint32_t read_done_tick;

    char tail_buf[128];
    uint16_t tail_len;

    uint32_t last_log_recv;
} OTA_HttpCtx_t;

static OTA_HttpCtx_t g_http;
/* 任务侧专用缓冲区：避免直接消费 ISR 正在写的 raw_buf */
static uint8_t g_http_proc_buf[OTA_HTTP_RAW_BUF_SZ];

static uint32_t OTA_HTTP_Align4(uint32_t v) { return (v + 3U) & ~3U; }

static void OTA_HTTP_ResetCommonFlags(void)
{
    g_http.ok_flag = 0;
    g_http.connect_flag = 0;
    g_http.qiact_ok = 0;
    g_http.qiact_query_done = 0;
    g_http.qiact_ip_ok = 0;
    g_http.get_ok = 0;
    g_http.read_ok = 0;
}

static void OTA_HTTP_Fail(const char* msg)
{
    if (msg && msg[0] != '\0') { boot_printf("%s\r\n", msg); }

    if (g_http.flash_started)
    {
        OTA_Flash_Abort();
        g_http.flash_started = 0;
    }

    g_http.error = 1;
    g_http.done = 0;
    g_http.busy = 0;
    g_http.state = OTA_HTTP_ST_ERROR;
}

void OTA_HTTP_Init(void)
{
    memset(&g_http, 0, sizeof(g_http));
    g_http.state = OTA_HTTP_ST_IDLE;
    g_http.get_err = -1;
    g_http.http_code = -1;
    g_http.content_len = -1;
}

void OTA_HTTP_Start(const char* url)
{
    if ((url == 0) || (url[0] == '\0')) return;

    memset(&g_http, 0, sizeof(g_http));
    g_http.state = OTA_HTTP_ST_SEND_QICSGP;
    g_http.busy = 1;
    g_http.done = 0;
    g_http.error = 0;
    g_http.get_err = -1;
    g_http.http_code = -1;
    g_http.content_len = -1;
    g_http.flash_started = 0;
    g_http.expected_written_len = 0;
    g_http.raw_len = 0;
    g_http.recv_len = 0;
    g_http.flash_fail = 0;
    g_http.read_done_seen = 0;
    g_http.read_done_tick = 0;
    g_http.tail_len = 0;
    g_http.last_log_recv = 0;
    g_http.last_rx_tick = HAL_GetTick();
    memset(g_http.tail_buf, 0, sizeof(g_http.tail_buf));

    strncpy(g_http.url, url, sizeof(g_http.url) - 1U);
    g_http.url[sizeof(g_http.url) - 1U] = '\0';

    boot_printf("[HTTP] start\r\n");
}

uint8_t OTA_HTTP_IsReadingRaw(void)
{ return (g_http.busy && (g_http.state == OTA_HTTP_ST_WAIT_HTTPREAD_DONE)) ? 1U : 0U; }

void OTA_HTTP_OnLine(const char* line)
{
    if ((line == 0) || (line[0] == '\0')) return;
    if (!g_http.busy) return;

    if (strncmp(line, "+CME ERROR:", 11) == 0)
    {
        g_http.error = 1;
        return;
    }

    if (strcmp(line, "ERROR") == 0)
    {
        g_http.error = 1;
        return;
    }

    if (strcmp(line, "OK") == 0)
    {
        g_http.ok_flag = 1;

        if (g_http.state == OTA_HTTP_ST_WAIT_QIACT_OK) { g_http.qiact_ok = 1; }

        return;
    }

    if ((strcmp(line, "CONNECT") == 0) || (strcmp(line, "ONNECT") == 0) || (strcmp(line, "NECT") == 0))
    {
        g_http.connect_flag = 1;

        if (g_http.state == OTA_HTTP_ST_WAIT_HTTPREAD_CONNECT)
        {
            uint32_t now = HAL_GetTick();
            g_http.ts = now;
            g_http.last_rx_tick = now;
            g_http.state = OTA_HTTP_ST_WAIT_HTTPREAD_DONE;
        }

        return;
    }

    if (strncmp(line, "+QIACT:", 7) == 0)
    {
        g_http.qiact_query_done = 1;
        if (strchr(line, '"') != 0) g_http.qiact_ip_ok = 1;
        return;
    }

    if (strncmp(line, "+QHTTPGET:", 10) == 0)
    {
        int err = -1;
        int http_code = -1;
        int content_len = -1;
        int cnt = sscanf(line, "+QHTTPGET: %d,%d,%d", &err, &http_code, &content_len);

        if (cnt >= 1)
        {
            g_http.get_err = err;
            if (cnt >= 2) g_http.http_code = http_code;
            if (cnt >= 3) g_http.content_len = content_len;

            if (err == 0)
                g_http.get_ok = 1;
            else
                g_http.error = 1;
        }
        else
        {
            g_http.error = 1;
        }
        return;
    }

    if (strncmp(line, "+QHTTPREAD:", 11) == 0)
    {
        int err = -1;
        if (sscanf(line, "+QHTTPREAD: %d", &err) == 1)
        {
            if (err == 0)
                g_http.read_ok = 1;
            else
                g_http.error = 1;
        }
        else
        {
            g_http.error = 1;
        }
        return;
    }
}

void OTA_HTTP_OnRawData(const uint8_t* data, uint16_t len)
{
    uint32_t fw_received;
    uint32_t fw_left;
    uint16_t fw_copy;
    uint16_t tail_copy;
    uint16_t i;

    if ((data == 0) || (len == 0)) return;
    if (!g_http.busy) return;
    if (g_http.state != OTA_HTTP_ST_WAIT_HTTPREAD_DONE) return;

    /* raw 收包保活时间戳，只给读超时用 */
    g_http.last_rx_tick = HAL_GetTick();

    fw_received = g_http.recv_len + g_http.raw_len;

    if ((g_http.content_len > 0) && (fw_received < (uint32_t)g_http.content_len))
        fw_left = (uint32_t)g_http.content_len - fw_received;
    else
        fw_left = 0U;

    fw_copy = len;
    if ((uint32_t)fw_copy > fw_left) fw_copy = (uint16_t)fw_left;

    if (fw_copy > 0U)
    {
        if ((g_http.raw_len + fw_copy) > OTA_HTTP_RAW_BUF_SZ)
        {
            g_http.flash_fail = 1;
            return;
        }

        memcpy(&g_http.raw_buf[g_http.raw_len], data, fw_copy);
        g_http.raw_len += fw_copy;
    }

    /* 超出固件长度的部分，按尾巴文本处理 */
    if (len > fw_copy)
    {
        tail_copy = len - fw_copy;

        if ((g_http.tail_len + tail_copy) >= sizeof(g_http.tail_buf))
            tail_copy = (sizeof(g_http.tail_buf) - 1U) - g_http.tail_len;

        if (tail_copy > 0U)
        {
            memcpy(&g_http.tail_buf[g_http.tail_len], &data[fw_copy], tail_copy);
            g_http.tail_len += tail_copy;
            g_http.tail_buf[g_http.tail_len] = '\0';
        }

        if (strstr(g_http.tail_buf, "+QHTTPREAD: 0") != 0)
            g_http.read_ok = 1;
        else if (strstr(g_http.tail_buf, "ERROR") != 0)
            g_http.error = 1;
    }

    if (g_http.tail_len > 96U)
    {
        for (i = 0; i < g_http.tail_len; i++)
        {
            if ((uint8_t)g_http.tail_buf[i] == '\n')
            {
                uint16_t remain = g_http.tail_len - i - 1U;
                memmove(g_http.tail_buf, &g_http.tail_buf[i + 1U], remain);
                g_http.tail_len = remain;
                g_http.tail_buf[g_http.tail_len] = '\0';
                break;
            }
        }
    }
}

void OTA_HTTP_Task(void)
{
    uint32_t now = HAL_GetTick();

    switch (g_http.state)
    {
        case OTA_HTTP_ST_IDLE:
        case OTA_HTTP_ST_DONE:
        case OTA_HTTP_ST_ERROR: break;

        case OTA_HTTP_ST_SEND_QICSGP:
            OTA_HTTP_ResetCommonFlags();
            OTA_EC_SendAT("AT+QICSGP=1,1,\"CMNET\",\"\",\"\",1");
            g_http.ts = now;
            g_http.state = OTA_HTTP_ST_WAIT_QICSGP;
            break;

        case OTA_HTTP_ST_WAIT_QICSGP:
            if (g_http.ok_flag)
                g_http.state = OTA_HTTP_ST_SEND_QIACT;
            else if (g_http.error || ((now - g_http.ts) >= OTA_HTTP_TIMEOUT_MS))
                OTA_HTTP_Fail("[HTTP] qicsgp fail");
            break;

        case OTA_HTTP_ST_SEND_QIACT:
            OTA_HTTP_ResetCommonFlags();
            OTA_EC_SendAT("AT+QIACT=1");
            g_http.ts = now;
            g_http.state = OTA_HTTP_ST_WAIT_QIACT_OK;
            break;

        case OTA_HTTP_ST_WAIT_QIACT_OK:
            if (g_http.qiact_ok)
                g_http.state = OTA_HTTP_ST_SEND_QIACT_QUERY;
            else if (g_http.error || ((now - g_http.ts) >= OTA_HTTP_TIMEOUT_MS))
                OTA_HTTP_Fail("[HTTP] qiact fail");
            break;

        case OTA_HTTP_ST_SEND_QIACT_QUERY:
            OTA_HTTP_ResetCommonFlags();
            OTA_EC_SendAT("AT+QIACT?");
            g_http.ts = now;
            g_http.state = OTA_HTTP_ST_WAIT_QIACT_QUERY;
            break;

        case OTA_HTTP_ST_WAIT_QIACT_QUERY:
            if (g_http.qiact_query_done && g_http.ok_flag)
            {
                if (g_http.qiact_ip_ok)
                    g_http.state = OTA_HTTP_ST_SEND_CFG_CTX;
                else
                    OTA_HTTP_Fail("[HTTP] qiact no ip");
            }
            else if (g_http.error || ((now - g_http.ts) >= OTA_HTTP_TIMEOUT_MS))
            {
                OTA_HTTP_Fail("[HTTP] qiact? fail");
            }
            break;

        case OTA_HTTP_ST_SEND_CFG_CTX:
            OTA_HTTP_ResetCommonFlags();
            OTA_EC_SendAT("AT+QHTTPCFG=\"contextid\",1");
            g_http.ts = now;
            g_http.state = OTA_HTTP_ST_WAIT_CFG_CTX;
            break;

        case OTA_HTTP_ST_WAIT_CFG_CTX:
            if (g_http.ok_flag)
                g_http.state = OTA_HTTP_ST_SEND_CFG_REQHDR;
            else if (g_http.error || ((now - g_http.ts) >= OTA_HTTP_TIMEOUT_MS))
                OTA_HTTP_Fail("[HTTP] cfg contextid fail");
            break;

        case OTA_HTTP_ST_SEND_CFG_REQHDR:
            OTA_HTTP_ResetCommonFlags();
            OTA_EC_SendAT("AT+QHTTPCFG=\"requestheader\",0");
            g_http.ts = now;
            g_http.state = OTA_HTTP_ST_WAIT_CFG_REQHDR;
            break;

        case OTA_HTTP_ST_WAIT_CFG_REQHDR:
            if (g_http.ok_flag)
                g_http.state = OTA_HTTP_ST_SEND_CFG_RSPHDR;
            else if (g_http.error || ((now - g_http.ts) >= OTA_HTTP_TIMEOUT_MS))
                OTA_HTTP_Fail("[HTTP] cfg requestheader fail");
            break;

        case OTA_HTTP_ST_SEND_CFG_RSPHDR:
            OTA_HTTP_ResetCommonFlags();
            OTA_EC_SendAT("AT+QHTTPCFG=\"responseheader\",0");
            g_http.ts = now;
            g_http.state = OTA_HTTP_ST_WAIT_CFG_RSPHDR;
            break;

        case OTA_HTTP_ST_WAIT_CFG_RSPHDR:
            if (g_http.ok_flag)
                g_http.state = OTA_HTTP_ST_SEND_URL_CMD;
            else if (g_http.error || ((now - g_http.ts) >= OTA_HTTP_TIMEOUT_MS))
                OTA_HTTP_Fail("[HTTP] cfg responseheader fail");
            break;

        case OTA_HTTP_ST_SEND_URL_CMD:
        {
            uint16_t url_len = (uint16_t)strlen(g_http.url);
            OTA_HTTP_ResetCommonFlags();
            OTA_EC_SendAT_F("AT+QHTTPURL=%u,30", url_len);
            g_http.ts = now;
            g_http.state = OTA_HTTP_ST_WAIT_URL_CONNECT;
            break;
        }

        case OTA_HTTP_ST_WAIT_URL_CONNECT:
            if (g_http.connect_flag)
                g_http.state = OTA_HTTP_ST_SEND_URL_DATA;
            else if (g_http.error || ((now - g_http.ts) >= OTA_HTTP_TIMEOUT_MS))
                OTA_HTTP_Fail("[HTTP] url connect fail");
            break;

        case OTA_HTTP_ST_SEND_URL_DATA:
        {
            uint16_t url_len = (uint16_t)strlen(g_http.url);
            HAL_UART_Transmit(&huart1, (uint8_t*)g_http.url, url_len, 1000);
            g_http.ts = HAL_GetTick();
            g_http.state = OTA_HTTP_ST_WAIT_URL_OK;
            break;
        }

        case OTA_HTTP_ST_WAIT_URL_OK:
            if (g_http.ok_flag)
                g_http.state = OTA_HTTP_ST_SEND_HTTPGET;
            else if (g_http.error || ((now - g_http.ts) >= OTA_HTTP_TIMEOUT_MS))
                OTA_HTTP_Fail("[HTTP] url send fail");
            break;

        case OTA_HTTP_ST_SEND_HTTPGET:
            OTA_HTTP_ResetCommonFlags();
            OTA_EC_SendAT("AT+QHTTPGET=30");
            g_http.ts = now;
            g_http.state = OTA_HTTP_ST_WAIT_HTTPGET;
            break;

        case OTA_HTTP_ST_WAIT_HTTPGET:
            if (g_http.get_ok)
            {
                if (g_http.content_len <= 0)
                {
                    boot_printf("[HTTP] invalid len=%d\r\n", g_http.content_len);
                    OTA_HTTP_Fail("[HTTP] invalid content length");
                    break;
                }

                if ((uint32_t)g_http.content_len > APP_MAX_SIZE)
                {
                    boot_printf("[HTTP] too large len=%d max=%lu\r\n", g_http.content_len, APP_MAX_SIZE);
                    OTA_HTTP_Fail("[HTTP] content too large");
                    break;
                }

                g_http.expected_written_len = OTA_HTTP_Align4((uint32_t)g_http.content_len);
                boot_printf("[HTTP] get ok, len=%d\r\n", g_http.content_len);
                g_http.state = OTA_HTTP_ST_SEND_HTTPREAD;
            }
            else if (g_http.error)
            {
                boot_printf("[HTTP] get fail, err=%d, code=%d\r\n", g_http.get_err, g_http.http_code);
                OTA_HTTP_Fail("[HTTP] get fail");
            }
            else if ((now - g_http.ts) >= OTA_HTTP_GET_TIMEOUT_MS) { OTA_HTTP_Fail("[HTTP] get timeout"); }
            break;

        case OTA_HTTP_ST_SEND_HTTPREAD:
            OTA_HTTP_ResetCommonFlags();
            g_http.raw_len = 0;
            g_http.tail_len = 0;
            memset(g_http.tail_buf, 0, sizeof(g_http.tail_buf));
            g_http.recv_len = 0;
            g_http.flash_fail = 0;
            g_http.read_done_seen = 0;
            g_http.read_done_tick = 0;
            g_http.last_log_recv = 0;
            g_http.last_rx_tick = HAL_GetTick();

            if (!OTA_Flash_Begin())
            {
                OTA_HTTP_Fail("[HTTP] flash begin fail");
                break;
            }

            g_http.flash_started = 1;
            OTA_EC_SendAT("AT+QHTTPREAD=30");
            g_http.ts = now;
            g_http.state = OTA_HTTP_ST_WAIT_HTTPREAD_CONNECT;
            break;

        case OTA_HTTP_ST_WAIT_HTTPREAD_CONNECT:
            if (g_http.connect_flag)
            {
                uint32_t tick_now = HAL_GetTick();
                g_http.ts = tick_now;
                g_http.last_rx_tick = tick_now;
                g_http.state = OTA_HTTP_ST_WAIT_HTTPREAD_DONE;
            }
            else if (g_http.error) { OTA_HTTP_Fail("[HTTP] read connect fail"); }
            else if ((now - g_http.ts) >= OTA_HTTP_TIMEOUT_MS) { OTA_HTTP_Fail("[HTTP] read connect timeout"); }
            break;

        case OTA_HTTP_ST_WAIT_HTTPREAD_DONE:
        {
            uint16_t pending_len = 0;
            uint32_t written = 0;

            __disable_irq();
            if (g_http.raw_len > 0U)
            {
                pending_len = g_http.raw_len;
                memcpy(g_http_proc_buf, g_http.raw_buf, pending_len);
                g_http.raw_len = 0;

                /* 先把这批字节记到 recv_len 里，再开中断 */
                g_http.recv_len += pending_len;
            }
            __enable_irq();

            if (pending_len > 0U)
            {
                if (!OTA_Flash_Write(g_http_proc_buf, pending_len)) { g_http.flash_fail = 1; }
                else
                {
                    if ((g_http.recv_len - g_http.last_log_recv) >= 4096U)
                    {
                        g_http.last_log_recv = g_http.recv_len;
                        boot_printf("[HTTP] recv=%lu/%d\r\n", g_http.recv_len, g_http.content_len);
                    }
                }
            }

            if (g_http.flash_fail)
            {
                boot_printf("[HTTP] flash write fail, recv=%lu\r\n", g_http.recv_len);
                OTA_HTTP_Fail("[HTTP] flash write fail");
                break;
            }

            if (g_http.read_ok && !g_http.read_done_seen)
            {
                g_http.read_done_seen = 1;
                g_http.read_done_tick = HAL_GetTick();
            }

            /*
             * 两种情况允许最终判定：
             * 1) 已经收满 content_len
             * 2) 已经看到 read_ok，并且额外等了一个小宽限期
             */
            if (((g_http.content_len > 0) && (g_http.recv_len >= (uint32_t)g_http.content_len)) ||
                (g_http.read_done_seen &&
                 ((int32_t)(HAL_GetTick() - g_http.read_done_tick) >= (int32_t)OTA_HTTP_READ_DONE_GRACE_MS)))
            {
                if (g_http.recv_len != (uint32_t)g_http.content_len)
                {
                    boot_printf("[HTTP] recv mismatch, recv=%lu expect=%d\r\n", g_http.recv_len, g_http.content_len);
                    OTA_HTTP_Fail("[HTTP] recv length mismatch");
                    break;
                }

                OTA_Flash_End();
                g_http.flash_started = 0;

                written = OTA_Flash_GetWrittenSize();
                if (written != g_http.expected_written_len)
                {
                    boot_printf("[HTTP] write mismatch, written=%lu expect=%lu\r\n", written,
                                g_http.expected_written_len);
                    OTA_HTTP_Fail("[HTTP] flash length mismatch");
                    break;
                }

                boot_printf("[HTTP] read ok, recv=%lu, expect=%d\r\n", g_http.recv_len, g_http.content_len);
                g_http.done = 1;
                g_http.busy = 0;
                g_http.state = OTA_HTTP_ST_DONE;
            }
            else if (g_http.error) { OTA_HTTP_Fail("[HTTP] read fail"); }
            else if ((int32_t)(HAL_GetTick() - g_http.last_rx_tick) >= (int32_t)OTA_HTTP_READ_TIMEOUT_MS)
            {
                OTA_HTTP_Fail("[HTTP] read timeout");
            }
            break;
        }

        default: OTA_HTTP_Fail("[HTTP] invalid state"); break;
    }
}

uint8_t OTA_HTTP_IsBusy(void) { return g_http.busy ? 1U : 0U; }
uint8_t OTA_HTTP_IsDone(void) { return g_http.done ? 1U : 0U; }
uint8_t OTA_HTTP_HasError(void) { return g_http.error ? 1U : 0U; }