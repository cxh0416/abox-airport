#ifndef __APP_RC_PRIV_H__
#define __APP_RC_PRIV_H__

#include <stdint.h>

#include "app_rc.h"
#include "bsp_can.h"
#include "can.h"
#include "gpio.h"
#include "motor485.h"
#include "rc_protocol.h"

extern RcApp_t g_rc_app;

extern uint8_t g_app_rc_prev_standby;
extern uint8_t g_app_rc_prev_super;
extern uint8_t g_app_rc_prev_main_page;
extern uint8_t g_app_rc_force_epb_apply;
extern uint32_t g_app_rc_exit_msg_deadline;
extern uint32_t g_app_rc_last_super_vib_tick;

extern uint8_t g_app_rc_prev_source_ok;
extern uint8_t g_app_rc_source_fault_estop_sent;
extern uint8_t g_app_rc_source_inited;

extern uint8_t g_app_rc_prev_estop_active;
extern uint8_t g_app_rc_estop_inited;

#define RC_UI_ID_LINE1 0x201U
#define RC_UI_ID_LINE2 0x301U
#define RC_UI_ID_LINE3 0x401U
#define RC_UI_ID_VIB 0x501U

#define RC_UI_EXIT_SHOW_MS 5000U
#define RC_VIB_FEEDBACK_TICK 50U
#define RC_VIB_FEEDBACK_REPEAT 2U
#define RC_VIB_SUPER_REFRESH_MS 50U
#define RC_VIB_SUPER_TICK 1U

void AppRcUi_Init(uint32_t now);
void AppRcUi_RequestShowText(const char* text);
void AppRcUi_RequestClearText(void);
void AppRcUi_RequestVib(uint8_t left_tick, uint8_t right_tick, uint8_t repeat);
void AppRcUi_SendVibNow(uint8_t left_tick, uint8_t right_tick);
void AppRcUi_ProcessCan1Ui(void);

void AppRcEmOut_Init(void);
void AppRcEmOut_RequestShortPress(void);
void AppRcEmOut_RequestLongPress(void);
void AppRcEmOut_Process(uint32_t now);

void AppRcLed_Init(uint32_t now);
void AppRcLed_Process(uint32_t now, uint8_t source_ok, uint8_t estop_active);

void AppRcMotorCtrl_Process(void);

#endif
