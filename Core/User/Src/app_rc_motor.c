#include "app_rc_priv.h"
#include "motor485.h"
#include "rc_protocol.h"

static uint8_t AppRcMotor_GetRemoteE(const RcData_t* rc)
{
    if (rc->car_super_mode) { return REMOTE_E_MODE_SUPER; }
    else if (rc->car_normal_mode) { return (rc->D1 & 0x40) ? REMOTE_E_MODE_NORMAL : REMOTE_E_MODE_TAKEOVER; }
    else
    {
        return REMOTE_E_MODE_NORMAL;
    }
}

static uint8_t AppRcMotor_GetSlave(const RcData_t* rc)
{
    uint8_t remote_e = AppRcMotor_GetRemoteE(rc);

    switch (remote_e)
    {
        case REMOTE_E_MODE_NORMAL: return 1U;
        case REMOTE_E_MODE_TAKEOVER: return 2U;
        case REMOTE_E_MODE_SUPER: return 3U;
        default: return 1U;
    }
}

void AppRcMotorCtrl_Process(void)
{
    static uint8_t s_last_slave = 0U;
    static uint8_t s_last_fn1 = 0U;
    static uint8_t s_last_fn2 = 0U;

    uint8_t slave;
    uint8_t fn1_now;
    uint8_t fn2_now;

    slave = AppRcMotor_GetSlave(&g_rc_app.rc);
    fn1_now = g_rc_app.rc.car_fn1 ? 1U : 0U;
    fn2_now = g_rc_app.rc.car_fn2 ? 1U : 0U;

    if (s_last_slave != slave)
    {
        s_last_slave = slave;
        s_last_fn1 = fn1_now;
        s_last_fn2 = fn2_now;
        return;
    }

    if ((!s_last_fn1) && fn1_now)
    {
        if (slave == 3U)
            Motor485_RequestSet(slave, MOTOR_CMD_REV);
        else
            Motor485_RequestSet(slave, MOTOR_CMD_FWD);
    }

    if ((!s_last_fn2) && fn2_now)
    {
        if (slave == 3U)
            Motor485_RequestSet(slave, MOTOR_CMD_FWD);
        else
            Motor485_RequestSet(slave, MOTOR_CMD_REV);
    }

    s_last_fn1 = fn1_now;
    s_last_fn2 = fn2_now;
}
