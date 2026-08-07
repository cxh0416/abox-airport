#include "ota_core.h"

#include "boot_cfg.h"
#include "boot_dbg.h"
#include "boot_jump.h"
#include "ec_power.h"
#include "main.h"
#include "ota_ec_at.h"
#include "ota_http.h"
#include "ota_info.h"

#define EC_BOOT_READY_DELAY_MS 8000U
#define OTA_CMD_TIMEOUT_MS 5000U
#define OTA_CPIN_RETRY_MAX 5U
#define OTA_CEREG_RETRY_MAX 20U
#define OTA_ERROR_RETRY_DELAY_MS 3000U

typedef enum
{
    OTA_CORE_ST_BOOT_CHECK = 0,
    OTA_CORE_ST_START_POWER,
    OTA_CORE_ST_WAIT_POWER_DONE,
    OTA_CORE_ST_WAIT_MODULE_READY,

    OTA_CORE_ST_SEND_AT,
    OTA_CORE_ST_WAIT_AT_OK,

    OTA_CORE_ST_SEND_ATE0,
    OTA_CORE_ST_WAIT_ATE0_OK,

    OTA_CORE_ST_SEND_CPIN,
    OTA_CORE_ST_WAIT_CPIN,

    OTA_CORE_ST_SEND_CSQ,
    OTA_CORE_ST_WAIT_CSQ,

    OTA_CORE_ST_SEND_CEREG,
    OTA_CORE_ST_WAIT_CEREG,

    OTA_CORE_ST_WAIT_OTA,
    OTA_CORE_ST_START_HTTP,
    OTA_CORE_ST_WAIT_HTTP,
    OTA_CORE_ST_JUMP_APP,
    OTA_CORE_ST_ERROR
} OtaCoreState_t;

static OtaCoreState_t g_ota_core_state = OTA_CORE_ST_BOOT_CHECK;
static uint32_t g_ota_core_ts = 0;
static uint8_t g_has_ota_req = 0;
static uint8_t g_app_valid = 0;
static uint8_t g_cpin_retry = 0;
static uint8_t g_cereg_retry = 0;
static uint8_t g_using_req_url = 0;

static void OTA_Core_RestartEc(const char* reason)
{
    if (reason && reason[0] != '\0') { boot_printf("%s\r\n", reason); }

    OTA_HTTP_Init();
    OTA_EC_AT_Init();
    OTA_EC_RegisterLineHook(OTA_HTTP_OnLine);
    OTA_EC_RegisterRawHook(OTA_HTTP_OnRawData);

    g_cpin_retry = 0;
    g_cereg_retry = 0;
    g_ota_core_state = OTA_CORE_ST_START_POWER;
}

void OTA_Core_Init(void)
{
    g_ota_core_state = OTA_CORE_ST_BOOT_CHECK;
    g_ota_core_ts = 0;
    g_has_ota_req = 0;
    g_app_valid = 0;
    g_cpin_retry = 0;
    g_cereg_retry = 0;

    OTA_EC_AT_Init();

    OTA_HTTP_Init();
    OTA_EC_RegisterLineHook(OTA_HTTP_OnLine);
    OTA_EC_RegisterRawHook(OTA_HTTP_OnRawData);
}

void OTA_Core_Task(void)
{
    EC_Power_Task();
    OTA_EC_AT_Task();
    OTA_HTTP_Task();

    switch (g_ota_core_state)
    {
        case OTA_CORE_ST_BOOT_CHECK:
            g_has_ota_req = OTA_Info_HasRequest();
            g_app_valid = Boot_IsAppValid(APP_START_ADDR) ? 1U : 0U;

            if (g_has_ota_req)
            {
                boot_printf("[BOOT] ota request found, fail_count=%lu\r\n", OTA_Info_GetReqFailCount());
                g_ota_core_state = OTA_CORE_ST_START_POWER;
            }
            else if (g_app_valid)
            {
                if (!OTA_Info_AppMarkedValid())
                {
                    boot_printf("[BOOT] app vector valid, mark app valid\r\n");
                    OTA_Info_MarkAppValid();
                }

                boot_printf("[BOOT] app valid, jump app\r\n");
                g_ota_core_state = OTA_CORE_ST_JUMP_APP;
            }
            else
            {
                boot_printf("[BOOT] no valid app, use default ota url\r\n");
                g_ota_core_state = OTA_CORE_ST_START_POWER;
            }
            break;

        case OTA_CORE_ST_START_POWER:
            boot_printf("[BOOT] start ec power\r\n");
            EC_Power_Init();
            EC_Power_StartRestartSequence();
            g_cpin_retry = 0;
            g_cereg_retry = 0;
            g_ota_core_state = OTA_CORE_ST_WAIT_POWER_DONE;
            break;

        case OTA_CORE_ST_WAIT_POWER_DONE:
            if (EC_Power_IsDone())
            {
                g_ota_core_ts = HAL_GetTick();
                boot_printf("[BOOT] ec power done\r\n");
                g_ota_core_state = OTA_CORE_ST_WAIT_MODULE_READY;
            }
            break;

        case OTA_CORE_ST_WAIT_MODULE_READY:
            if ((HAL_GetTick() - g_ota_core_ts) >= EC_BOOT_READY_DELAY_MS) { g_ota_core_state = OTA_CORE_ST_SEND_AT; }
            break;

        case OTA_CORE_ST_SEND_AT:
            OTA_EC_TestAT();
            g_ota_core_ts = HAL_GetTick();
            g_ota_core_state = OTA_CORE_ST_WAIT_AT_OK;
            break;

        case OTA_CORE_ST_WAIT_AT_OK:
            if (OTA_EC_IsReady())
            {
                boot_printf("[BOOT] ec at ok\r\n");
                g_ota_core_state = OTA_CORE_ST_SEND_ATE0;
            }
            else if ((HAL_GetTick() - g_ota_core_ts) >= OTA_CMD_TIMEOUT_MS)
            {
                OTA_Core_RestartEc("[BOOT] ec at fail, repower ec");
            }
            break;

        case OTA_CORE_ST_SEND_ATE0:
            OTA_EC_RequestEchoOff();
            g_ota_core_ts = HAL_GetTick();
            g_ota_core_state = OTA_CORE_ST_WAIT_ATE0_OK;
            break;

        case OTA_CORE_ST_WAIT_ATE0_OK:
            if (OTA_EC_IsEchoOffDone())
            {
                boot_printf("[BOOT] echo off ok\r\n");
                g_ota_core_state = OTA_CORE_ST_SEND_CPIN;
            }
            else if ((HAL_GetTick() - g_ota_core_ts) >= OTA_CMD_TIMEOUT_MS)
            {
                OTA_Core_RestartEc("[BOOT] echo off timeout, repower ec");
            }
            break;

        case OTA_CORE_ST_SEND_CPIN:
            OTA_EC_RequestCpin();
            g_ota_core_ts = HAL_GetTick();
            g_ota_core_state = OTA_CORE_ST_WAIT_CPIN;
            break;

        case OTA_CORE_ST_WAIT_CPIN:
            if (OTA_EC_IsCpinDone())
            {
                if (OTA_EC_IsSimReady())
                {
                    boot_printf("[BOOT] sim ready\r\n");
                    g_cpin_retry = 0;
                    g_ota_core_state = OTA_CORE_ST_SEND_CSQ;
                }
                else
                {
                    if (g_cpin_retry < OTA_CPIN_RETRY_MAX)
                    {
                        g_cpin_retry++;
                        g_ota_core_state = OTA_CORE_ST_SEND_CPIN;
                    }
                    else
                    {
                        OTA_Core_RestartEc("[BOOT] sim not ready, repower ec");
                    }
                }
            }
            else if (OTA_EC_HasCmeError())
            {
                if (g_cpin_retry < OTA_CPIN_RETRY_MAX)
                {
                    g_cpin_retry++;
                    g_ota_core_state = OTA_CORE_ST_SEND_CPIN;
                }
                else
                {
                    boot_printf("[BOOT] cpin fail, cme=%d\r\n", OTA_EC_GetCmeErrorCode());
                    OTA_Core_RestartEc("[BOOT] cpin fail, repower ec");
                }
            }
            else if ((HAL_GetTick() - g_ota_core_ts) >= OTA_CMD_TIMEOUT_MS)
            {
                if (g_cpin_retry < OTA_CPIN_RETRY_MAX)
                {
                    g_cpin_retry++;
                    g_ota_core_state = OTA_CORE_ST_SEND_CPIN;
                }
                else
                {
                    OTA_Core_RestartEc("[BOOT] cpin timeout, repower ec");
                }
            }
            break;

        case OTA_CORE_ST_SEND_CSQ:
            OTA_EC_RequestCsq();
            g_ota_core_ts = HAL_GetTick();
            g_ota_core_state = OTA_CORE_ST_WAIT_CSQ;
            break;

        case OTA_CORE_ST_WAIT_CSQ:
            if (OTA_EC_IsCsqDone())
            {
                boot_printf("[BOOT] csq=%d\r\n", OTA_EC_GetCsq());
                g_ota_core_state = OTA_CORE_ST_SEND_CEREG;
            }
            else if (OTA_EC_HasCmeError())
            {
                boot_printf("[BOOT] csq cme=%d\r\n", OTA_EC_GetCmeErrorCode());
                OTA_Core_RestartEc("[BOOT] csq cme, repower ec");
            }
            else if ((HAL_GetTick() - g_ota_core_ts) >= OTA_CMD_TIMEOUT_MS)
            {
                OTA_Core_RestartEc("[BOOT] csq timeout, repower ec");
            }
            break;

        case OTA_CORE_ST_SEND_CEREG:
            OTA_EC_RequestCereg();
            g_ota_core_ts = HAL_GetTick();
            g_ota_core_state = OTA_CORE_ST_WAIT_CEREG;
            break;

        case OTA_CORE_ST_WAIT_CEREG:
            if (OTA_EC_IsCeregDone())
            {
                if (OTA_EC_IsNetReady())
                {
                    boot_printf("[BOOT] net ready, stat=%d\r\n", OTA_EC_GetCeregStat());
                    g_cereg_retry = 0;
                    g_ota_core_state = OTA_CORE_ST_WAIT_OTA;
                }
                else
                {
                    if (g_cereg_retry < OTA_CEREG_RETRY_MAX)
                    {
                        g_cereg_retry++;
                        g_ota_core_state = OTA_CORE_ST_SEND_CEREG;
                    }
                    else
                    {
                        boot_printf("[BOOT] net not ready, stat=%d\r\n", OTA_EC_GetCeregStat());
                        OTA_Core_RestartEc("[BOOT] net not ready, repower ec");
                    }
                }
            }
            else if ((HAL_GetTick() - g_ota_core_ts) >= OTA_CMD_TIMEOUT_MS)
            {
                if (g_cereg_retry < OTA_CEREG_RETRY_MAX)
                {
                    g_cereg_retry++;
                    g_ota_core_state = OTA_CORE_ST_SEND_CEREG;
                }
                else
                {
                    OTA_Core_RestartEc("[BOOT] cereg timeout, repower ec");
                }
            }
            break;

        case OTA_CORE_ST_WAIT_OTA:
            boot_printf("[BOOT] start http ota\r\n");
            g_ota_core_state = OTA_CORE_ST_START_HTTP;
            break;

        case OTA_CORE_ST_START_HTTP:
        {
            OTA_Info_t info;
            const char* url = 0;

            OTA_Info_Load(&info);

            if (OTA_Info_IsRequestValid(&info))
            {
                url = info.url;
                g_using_req_url = 1;
                boot_printf("[BOOT] use req url: %s\r\n", url);
            }
            else
            {
                url = OTA_DEFAULT_URL;
                g_using_req_url = 0;
                boot_printf("[BOOT] use default url: %s\r\n", url);
            }

            OTA_HTTP_Start(url);
            g_ota_core_state = OTA_CORE_ST_WAIT_HTTP;
            break;
        }

        case OTA_CORE_ST_WAIT_HTTP:
            if (OTA_HTTP_IsDone())
            {
                boot_printf("[BOOT] http download done\r\n");

                if (Boot_IsAppValid(APP_START_ADDR))
                {
                    OTA_Info_ClearReqFailCount();
                    OTA_Info_MarkAppValid();
                    OTA_Info_ClearRequest();

                    boot_printf("[BOOT] new app valid, system reset\r\n");
                    HAL_Delay(100);
                    NVIC_SystemReset();
                }
                else
                {
                    boot_printf("[BOOT] downloaded app invalid\r\n");

                    if (g_using_req_url)
                    {
                        uint32_t fail_count;

                        OTA_Info_IncReqFailCount();
                        fail_count = OTA_Info_GetReqFailCount();

                        boot_printf("[BOOT] req url invalid app, count=%lu\r\n", fail_count);

                        if (fail_count >= OTA_REQ_FAIL_MAX)
                        {
                            boot_printf("[BOOT] req url invalid too much, clear request\r\n");
                            OTA_Info_ClearReqFailCount();
                            OTA_Info_ClearRequest();
                        }
                    }

                    OTA_Core_RestartEc("[BOOT] downloaded app invalid, retry");
                }
            }
            else if (OTA_HTTP_HasError())
            {
                if (g_using_req_url)
                {
                    uint32_t fail_count;

                    OTA_Info_IncReqFailCount();
                    fail_count = OTA_Info_GetReqFailCount();

                    boot_printf("[BOOT] req url http fail, count=%lu\r\n", fail_count);

                    if (fail_count >= OTA_REQ_FAIL_MAX)
                    {
                        boot_printf("[BOOT] req url fail too much, clear request\r\n");
                        OTA_Info_ClearReqFailCount();
                        OTA_Info_ClearRequest();
                    }
                }

                OTA_Core_RestartEc("[BOOT] http download fail, repower ec");
            }
            break;

        case OTA_CORE_ST_JUMP_APP:
            if (Boot_IsAppValid(APP_START_ADDR))
            {
                boot_printf("[BOOT] jump app\r\n");
                Boot_JumpToApp(APP_START_ADDR);
            }
            else
            {
                OTA_Core_RestartEc("[BOOT] app invalid before jump, retry");
            }
            break;

        case OTA_CORE_ST_ERROR:
            if ((HAL_GetTick() - g_ota_core_ts) >= OTA_ERROR_RETRY_DELAY_MS)
            {
                OTA_Core_RestartEc("[BOOT] error state retry");
            }
            break;

        default:
            g_ota_core_ts = HAL_GetTick();
            g_ota_core_state = OTA_CORE_ST_ERROR;
            break;
    }
}
