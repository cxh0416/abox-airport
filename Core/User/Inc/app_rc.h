#ifndef __APP_RC_H__
#define __APP_RC_H__

#include <stdint.h>

#include "rc_logic.h"
#include "stm32f1xx_hal.h"

extern RcApp_t g_rc_app;

void AppRc_Init(uint32_t now);
void AppRc_OnCanRx(CAN_HandleTypeDef* hcan, CAN_RxHeaderTypeDef* rxHeader, uint8_t data[8]);
void AppRc_Proc_5ms(uint32_t now);
uint8_t AppRc_IsMotorCtrlActive(void);

#endif
