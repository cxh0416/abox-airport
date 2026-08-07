#include "rc_logic.h"

#include <string.h>

static uint8_t RC_IsStickActive(int16_t v) { return (v > RC_DEADZONE_ACTIVE) || (v < -RC_DEADZONE_ACTIVE); }

static uint8_t RC_GetLxDir(int16_t lx)
{
    if (lx < -RC_DEADZONE_WAKE) return 0;  // 左
    if (lx > RC_DEADZONE_WAKE) return 2;   // 右
    return 1;                              // 中
}

static uint8_t RC_HasUserOperation(const RcData_t* now, const RcData_t* prev)
{
    /* 1. 摇杆数值有明显变化，算操作 */
    if ((now->lx > prev->lx + RC_OP_DELTA) || (now->lx < prev->lx - RC_OP_DELTA)) return 1;
    if ((now->ly > prev->ly + RC_OP_DELTA) || (now->ly < prev->ly - RC_OP_DELTA)) return 1;
    if ((now->rx > prev->rx + RC_OP_DELTA) || (now->rx < prev->rx - RC_OP_DELTA)) return 1;
    if ((now->ry > prev->ry + RC_OP_DELTA) || (now->ry < prev->ry - RC_OP_DELTA)) return 1;

    /* 2. 摇杆虽然没变化，但只要仍明显偏离中位，也认为用户仍在操作 */
    if (RC_IsStickActive(now->lx)) return 1;
    if (RC_IsStickActive(now->ly)) return 1;
    if (RC_IsStickActive(now->rx)) return 1;
    if (RC_IsStickActive(now->ry)) return 1;

    /* 3. 按键/功能位变化，算操作 */
    if (now->D1 != prev->D1) return 1;

    if (now->fun_main_page != prev->fun_main_page) return 1;
    if (now->fun_estop_ok != prev->fun_estop_ok) return 1;
    if (now->fun_rf_ok != prev->fun_rf_ok) return 1;
    if (now->car_enable != prev->car_enable) return 1;
    if (now->car_horn != prev->car_horn) return 1;
    if (now->car_epb != prev->car_epb) return 1;
    if (now->car_auto_mode != prev->car_auto_mode) return 1;
    if (now->car_fn1 != prev->car_fn1) return 1;
    if (now->car_fn2 != prev->car_fn2) return 1;
    if (now->car_normal_mode != prev->car_normal_mode) return 1;
    if (now->car_super_mode != prev->car_super_mode) return 1;

    return 0;
}

void RC_AppInit(RcApp_t* app, uint32_t now)
{
    memset(app, 0, sizeof(*app));
    app->last_user_op_tick = now;
    app->lx_dir_prev = RC_GetLxDir(app->rc.lx);
    app->wake_step = 0;
}

void RC_UpdateStandby(RcApp_t* app, uint32_t now)
{
    uint8_t has_op = RC_HasUserOperation(&app->rc, &app->rc_prev);

    if (!app->standby_mode)
    {
        /* 只要检测到用户操作，就刷新最后操作时间 */
        if (has_op) { app->last_user_op_tick = now; }

        /* 超时无操作，进入待机 */
        if ((now - app->last_user_op_tick) >= STANDBY_TIMEOUT_MS)
        {
            app->standby_mode = 1;
            app->wake_step = 0;
            app->lx_dir_prev = RC_GetLxDir(app->rc.lx);
        }
    }
    else
    {
        uint8_t lx_dir_now = RC_GetLxDir(app->rc.lx);

        if (lx_dir_now != app->lx_dir_prev)
        {
            /* 忽略中位，只识别 左->右->左 */
            if ((lx_dir_now == 0) || (lx_dir_now == 2))
            {
                switch (app->wake_step)
                {
                    case 0:
                        /* 第一步：左 */
                        if (lx_dir_now == 0) { app->wake_step = 1; }
                        break;

                    case 1:
                        /* 第二步：右 */
                        if (lx_dir_now == 2) { app->wake_step = 2; }
                        else if (lx_dir_now == 0) { app->wake_step = 1; }
                        break;

                    case 2:
                        /* 第三步：左，解除待机 */
                        if (lx_dir_now == 0)
                        {
                            app->standby_mode = 0;
                            app->last_user_op_tick = now;
                            app->wake_step = 0;
                        }
                        else if (lx_dir_now == 2) { app->wake_step = 2; }
                        break;

                    default: app->wake_step = 0; break;
                }
            }
        }

        app->lx_dir_prev = lx_dir_now;
    }

    /* 非待机状态下，持续更新上一帧 */
    if (!app->standby_mode)
    {
        app->rc_prev = app->rc;
        app->lx_dir_prev = RC_GetLxDir(app->rc.lx);
    }
}

uint8_t RC_IsLinkOk(const RcApp_t* app, uint32_t now)
{
    uint8_t timeout_181 = (now - app->last_181_tick > RC_TIMEOUT_MS);
    uint8_t timeout_281 = (now - app->last_281_tick > RC_TIMEOUT_MS);
    return !(timeout_181 || timeout_281);
}

uint8_t RC_IsRemoteStateOk(const RcApp_t* app)
{
    /* 这里只判断遥控链路/遥控状态是否正常，
       不再把“是否主页”算成 source 是否有效 */
    return (app->rc.fun_rf_ok != 0);
}

uint8_t RC_IsRemoteEstopOk(const RcApp_t* app) { return (app->rc.fun_estop_ok != 0); }

uint8_t RC_IsSourceOk(const RcApp_t* app, uint32_t now) { return RC_IsLinkOk(app, now) && RC_IsRemoteStateOk(app); }

uint8_t RC_IsEstopActive(const RcApp_t* app, uint8_t hw_estop_ok)
{ return (!RC_IsRemoteEstopOk(app)) || (!hw_estop_ok); }
