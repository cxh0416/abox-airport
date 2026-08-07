#ifndef __RC_LOGIC_H__
#define __RC_LOGIC_H__

#include <stdint.h>

#include "rc_types.h"

#define STANDBY_TIMEOUT_MS 10000U

/* 持续操作判断死区：只要摇杆明显偏离中位，就认为仍在操作 */
#define RC_DEADZONE_ACTIVE 150

/* 待机唤醒手势死区：用于识别 lx 左/中/右，阈值可适当大一些防误触 */
#define RC_DEADZONE_WAKE 500

/* 摇杆变化量判断阈值：相对上一帧变化超过该值，认为有操作 */
#define RC_OP_DELTA 30

#define RC_TIMEOUT_MS 200U

typedef struct
{
    RcData_t rc;
    RcData_t rc_prev;

    uint32_t last_181_tick;
    uint32_t last_281_tick;
    uint32_t last_user_op_tick;

    uint8_t standby_mode;
    uint8_t lx_dir_prev;
    uint8_t rc_valid;
    uint8_t wake_step;
} RcApp_t;

void RC_AppInit(RcApp_t* app, uint32_t now);
void RC_UpdateStandby(RcApp_t* app, uint32_t now);

uint8_t RC_IsLinkOk(const RcApp_t* app, uint32_t now);
uint8_t RC_IsRemoteStateOk(const RcApp_t* app);
uint8_t RC_IsRemoteEstopOk(const RcApp_t* app);

uint8_t RC_IsSourceOk(const RcApp_t* app, uint32_t now);
uint8_t RC_IsEstopActive(const RcApp_t* app, uint8_t hw_estop_ok);

#endif
