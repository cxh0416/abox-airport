#include "app_rc.h"

#include <string.h>

#include "app_rc_priv.h"
#include "bsp_can.h"
#include "can.h"
#include "gpio.h"
#include "main.h"
#include "rc_protocol.h"

RcApp_t g_rc_app;

uint8_t g_app_rc_prev_standby = 0;
uint8_t g_app_rc_prev_super = 0;
uint8_t g_app_rc_prev_main_page = 1U;
uint8_t g_app_rc_force_epb_apply = 0U;
uint32_t g_app_rc_exit_msg_deadline = 0;
uint32_t g_app_rc_last_super_vib_tick = 0;

uint8_t g_app_rc_prev_source_ok = 1U;
uint8_t g_app_rc_source_fault_estop_sent = 0U;
uint8_t g_app_rc_source_inited = 0U;

uint8_t g_app_rc_prev_estop_active = 0U;
uint8_t g_app_rc_estop_inited = 0U;

void AppRc_Init(uint32_t now)
{
    RC_AppInit(&g_rc_app, now);

    g_app_rc_prev_standby = 0;
    g_app_rc_prev_super = 0;
    g_app_rc_prev_main_page = 1U;
    g_app_rc_force_epb_apply = 0U;
    g_app_rc_exit_msg_deadline = 0U;
    g_app_rc_last_super_vib_tick = now;

    g_app_rc_prev_source_ok = 1U;
    g_app_rc_source_fault_estop_sent = 0U;
    g_app_rc_source_inited = 0U;

    g_app_rc_prev_estop_active = 0U;
    g_app_rc_estop_inited = 0U;

    AppRcUi_Init(now);
    AppRcEmOut_Init();
    AppRcLed_Init(now);
}

void AppRc_OnCanRx(CAN_HandleTypeDef* hcan, CAN_RxHeaderTypeDef* rxHeader, uint8_t data[8])
{
    if (hcan->Instance != CAN1) return;

    if (rxHeader->StdId == 0x181)
    {
        RC_Parse181(&g_rc_app.rc, data);
        g_rc_app.last_181_tick = HAL_GetTick();
    }
    else if (rxHeader->StdId == 0x281)
    {
        RC_Parse281(&g_rc_app.rc, data);
        g_rc_app.last_281_tick = HAL_GetTick();
    }
}

void AppRc_Proc_5ms(uint32_t now)
{
    static uint8_t phase = 0;
    uint8_t tx[8];

    uint8_t hw_estop_ok;
    uint8_t source_ok;
    uint8_t estop_active;
    uint8_t standby_now;
    uint8_t super_now;
    uint8_t main_page_now;

    RC_UpdateStandby(&g_rc_app, now);

    hw_estop_ok = (HAL_GPIO_ReadPin(IO_IN4_GPIO_Port, IO_IN4_Pin) == GPIO_PIN_RESET);
    source_ok = RC_IsSourceOk(&g_rc_app, now);
    estop_active = RC_IsEstopActive(&g_rc_app, hw_estop_ok);
    g_rc_app.rc_valid = source_ok;

    AppRcLed_Process(now, source_ok, estop_active);
    AppRcEmOut_Process(now);

    if (!g_app_rc_estop_inited)
    {
        g_app_rc_prev_estop_active = estop_active;
        g_app_rc_estop_inited = 1U;
    }
    else
    {
        if ((!g_app_rc_prev_estop_active) && estop_active) { AppRcEmOut_RequestShortPress(); }
        else if (g_app_rc_prev_estop_active && (!estop_active)) { AppRcEmOut_RequestLongPress(); }

        if (g_app_rc_prev_estop_active != estop_active) { g_app_rc_prev_estop_active = estop_active; }
    }

    if (!g_app_rc_source_inited)
    {
        g_app_rc_prev_source_ok = source_ok;
        g_app_rc_source_inited = 1U;
    }
    else
    {
        if (g_app_rc_prev_source_ok && (!source_ok))
        {
            if (!g_app_rc_source_fault_estop_sent)
            {
                AppRcEmOut_RequestShortPress();
                g_app_rc_source_fault_estop_sent = 1U;
            }
        }
        else if ((!g_app_rc_prev_source_ok) && source_ok) { g_app_rc_source_fault_estop_sent = 0U; }

        g_app_rc_prev_source_ok = source_ok;
    }

    standby_now = g_rc_app.standby_mode;
    super_now = g_rc_app.rc.car_super_mode;
    main_page_now = g_rc_app.rc.fun_main_page;

    if (g_app_rc_prev_main_page && (!main_page_now)) { g_app_rc_force_epb_apply = 1U; }
    else if ((!g_app_rc_prev_main_page) && main_page_now) { g_app_rc_force_epb_apply = 0U; }
    g_app_rc_prev_main_page = main_page_now;

    if (!main_page_now) { AppRcMotorCtrl_Process(); }

    if ((!g_app_rc_prev_standby) && standby_now)
    {
        g_app_rc_exit_msg_deadline = 0U;
        AppRcUi_RequestShowText("STANDBY");
    }
    else if (g_app_rc_prev_standby && (!standby_now))
    {
        AppRcUi_RequestShowText("STANDBY OFF");
        g_app_rc_exit_msg_deadline = now + RC_UI_EXIT_SHOW_MS;
        AppRcUi_RequestVib(RC_VIB_FEEDBACK_TICK, RC_VIB_FEEDBACK_TICK, RC_VIB_FEEDBACK_REPEAT);
    }
    g_app_rc_prev_standby = standby_now;

    if ((g_app_rc_exit_msg_deadline != 0U) && ((int32_t)(now - g_app_rc_exit_msg_deadline) >= 0))
    {
        AppRcUi_RequestClearText();
        g_app_rc_exit_msg_deadline = 0U;
    }

    AppRcUi_ProcessCan1Ui();

    if (super_now)
    {
        if ((now - g_app_rc_last_super_vib_tick) >= RC_VIB_SUPER_REFRESH_MS)
        {
            AppRcUi_SendVibNow(RC_VIB_SUPER_TICK, RC_VIB_SUPER_TICK);
            g_app_rc_last_super_vib_tick = now;
        }
    }
    else if (g_app_rc_prev_super) { AppRcUi_SendVibNow(0, 0); }
    g_app_rc_prev_super = super_now;

    if (!source_ok) { return; }

    if (estop_active || g_rc_app.standby_mode || (!main_page_now)) { RC_Build10B_Standby(tx); }
    else
    {
        RC_Build10B(&g_rc_app.rc, tx);
    }

    if (phase == 0)
    {
        RcData_t rc_tx = g_rc_app.rc;

        if (g_app_rc_force_epb_apply) { rc_tx.car_epb = 1U; }

        if (!main_page_now)
        {
            rc_tx.car_fn1 = 0U;
            rc_tx.car_fn2 = 0U;
        }

        RC_Build10A(&rc_tx, tx);
        BSP_CAN_SendStd(&hcan2, 0x10A, tx, 8);
        phase = 1;
    }
    else
    {
        BSP_CAN_SendStd(&hcan2, 0x10B, tx, 8);
        phase = 0;
    }
}

uint8_t AppRc_IsMotorCtrlActive(void) { return (g_rc_app.rc_valid && (!g_rc_app.rc.fun_main_page)) ? 1U : 0U; }
