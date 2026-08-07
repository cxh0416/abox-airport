#include <stdint.h>

#include "app_rc_priv.h"
#include "gpio.h"
#include "main.h"

typedef enum
{
    EM_ACT_IDLE = 0,
    EM_ACT_PRESSING,
    EM_ACT_RESTART_GAP
} EmActState_t;

typedef enum
{
    EM_CMD_NONE = 0,
    EM_CMD_SHORT_PRESS,
    EM_CMD_LONG_PRESS
} EmCmd_t;

#define EM_OUT_PRESS_LEVEL GPIO_PIN_SET
#define EM_OUT_RELEASE_LEVEL GPIO_PIN_RESET
#define EM_OUT_SHORT_PRESS_MS 200U
#define EM_OUT_LONG_PRESS_MS 7500U
#define EM_OUT_RESTART_GAP_MS 30U

static EmActState_t s_em_state = EM_ACT_IDLE;
static EmCmd_t s_em_cmd = EM_CMD_NONE;
static uint32_t s_em_deadline = 0;
static EmCmd_t s_em_restart_cmd = EM_CMD_NONE;
static uint32_t s_em_restart_duration = 0;

static void AppRcEmOut_Release(void) { HAL_GPIO_WritePin(RELAY_CTL_GPIO_Port, RELAY_CTL_Pin, EM_OUT_RELEASE_LEVEL); }

static void AppRcEmOut_Press(void) { HAL_GPIO_WritePin(RELAY_CTL_GPIO_Port, RELAY_CTL_Pin, EM_OUT_PRESS_LEVEL); }

static void AppRcEmOut_StartPress(EmCmd_t cmd, uint32_t duration_ms)
{
    s_em_cmd = cmd;
    s_em_state = EM_ACT_PRESSING;
    s_em_deadline = HAL_GetTick() + duration_ms;
    AppRcEmOut_Press();
}

static void AppRcEmOut_RestartWithGap(EmCmd_t cmd, uint32_t duration_ms)
{
    AppRcEmOut_Release();

    s_em_restart_cmd = cmd;
    s_em_restart_duration = duration_ms;
    s_em_cmd = EM_CMD_NONE;
    s_em_state = EM_ACT_RESTART_GAP;
    s_em_deadline = HAL_GetTick() + EM_OUT_RESTART_GAP_MS;
}

void AppRcEmOut_Init(void)
{
    s_em_state = EM_ACT_IDLE;
    s_em_cmd = EM_CMD_NONE;
    s_em_deadline = 0;
    s_em_restart_cmd = EM_CMD_NONE;
    s_em_restart_duration = 0U;
    AppRcEmOut_Release();
}

void AppRcEmOut_RequestShortPress(void)
{
    if (s_em_state != EM_ACT_IDLE)
    {
        if ((s_em_state == EM_ACT_PRESSING) && (s_em_cmd == EM_CMD_LONG_PRESS))
        {
            AppRcEmOut_RestartWithGap(EM_CMD_SHORT_PRESS, EM_OUT_SHORT_PRESS_MS);
            return;
        }
        return;
    }

    AppRcEmOut_StartPress(EM_CMD_SHORT_PRESS, EM_OUT_SHORT_PRESS_MS);
}

void AppRcEmOut_RequestLongPress(void)
{
    if (s_em_state != EM_ACT_IDLE) return;
    AppRcEmOut_StartPress(EM_CMD_LONG_PRESS, EM_OUT_LONG_PRESS_MS);
}

void AppRcEmOut_Process(uint32_t now)
{
    if (s_em_state == EM_ACT_PRESSING)
    {
        if ((int32_t)(now - s_em_deadline) >= 0)
        {
            AppRcEmOut_Release();
            s_em_state = EM_ACT_IDLE;
            s_em_cmd = EM_CMD_NONE;
        }
    }
    else if (s_em_state == EM_ACT_RESTART_GAP)
    {
        if ((int32_t)(now - s_em_deadline) >= 0)
        {
            AppRcEmOut_StartPress(s_em_restart_cmd, s_em_restart_duration);
            s_em_restart_cmd = EM_CMD_NONE;
            s_em_restart_duration = 0U;
        }
    }
}
