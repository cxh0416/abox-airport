#include <string.h>

#include "app_rc_priv.h"
#include "bsp_can.h"
#include "can.h"

typedef enum
{
    RC_UI_JOB_NONE = 0,
    RC_UI_JOB_SHOW_TEXT,
    RC_UI_JOB_CLEAR
} RcUiJob_t;

static char s_ui_line[20];
static uint8_t s_ui_tx_step = 0;
static RcUiJob_t s_ui_job = RC_UI_JOB_NONE;
static uint8_t s_vib_pending_repeat = 0;
static uint8_t s_vib_pending_left = 0;
static uint8_t s_vib_pending_right = 0;

static void AppRcUi_BuildCenteredLine(char out[20], const char* text)
{
    uint8_t i;
    uint8_t len = 0;
    uint8_t start;

    for (i = 0; i < 20; i++) out[i] = ' ';

    while ((text[len] != '\0') && (len < 20)) len++;

    if (len >= 20)
    {
        memcpy(out, text, 20);
        return;
    }

    start = (uint8_t)((20U - len) / 2U);
    memcpy(&out[start], text, len);
}

void AppRcUi_Init(uint32_t now)
{
    (void)now;
    memset(s_ui_line, ' ', sizeof(s_ui_line));
    s_ui_tx_step = 0;
    s_ui_job = RC_UI_JOB_NONE;
    s_vib_pending_repeat = 0;
    s_vib_pending_left = 0;
    s_vib_pending_right = 0;
}

void AppRcUi_RequestShowText(const char* text)
{
    AppRcUi_BuildCenteredLine(s_ui_line, text);
    s_ui_job = RC_UI_JOB_SHOW_TEXT;
    s_ui_tx_step = 1;
}

void AppRcUi_RequestClearText(void)
{
    uint8_t i;
    for (i = 0; i < 20; i++) s_ui_line[i] = ' ';
    s_ui_job = RC_UI_JOB_CLEAR;
    s_ui_tx_step = 1;
}

void AppRcUi_RequestVib(uint8_t left_tick, uint8_t right_tick, uint8_t repeat)
{
    s_vib_pending_left = left_tick;
    s_vib_pending_right = right_tick;
    s_vib_pending_repeat = repeat;
}

void AppRcUi_SendVibNow(uint8_t left_tick, uint8_t right_tick)
{
    uint8_t tx[8] = {0};

    tx[0] = left_tick;
    tx[1] = right_tick;

    BSP_CAN_SendStd(&hcan1, RC_UI_ID_VIB, tx, 8);
}

void AppRcUi_ProcessCan1Ui(void)
{
    uint8_t tx[8];

    if (s_ui_tx_step == 1)
    {
        memcpy(tx, &s_ui_line[0], 8);
        BSP_CAN_SendStd(&hcan1, RC_UI_ID_LINE1, tx, 8);
        s_ui_tx_step = 2;
        return;
    }

    if (s_ui_tx_step == 2)
    {
        memcpy(tx, &s_ui_line[8], 8);
        BSP_CAN_SendStd(&hcan1, RC_UI_ID_LINE2, tx, 8);
        s_ui_tx_step = 3;
        return;
    }

    if (s_ui_tx_step == 3)
    {
        tx[0] = (uint8_t)s_ui_line[16];
        tx[1] = (uint8_t)s_ui_line[17];
        tx[2] = (uint8_t)s_ui_line[18];
        tx[3] = (uint8_t)s_ui_line[19];
        tx[4] = 0x00;
        tx[5] = 0x00;
        tx[6] = 0x00;
        tx[7] = 0x00;
        BSP_CAN_SendStd(&hcan1, RC_UI_ID_LINE3, tx, 8);

        s_ui_tx_step = 0;
        s_ui_job = RC_UI_JOB_NONE;
        return;
    }

    if (s_vib_pending_repeat > 0)
    {
        AppRcUi_SendVibNow(s_vib_pending_left, s_vib_pending_right);
        s_vib_pending_repeat--;
    }
}
