#include "rc_protocol.h"

#include <string.h>

static int16_t RC_Scale1000To450(int16_t v)
{
    int32_t x = v;

    if (x > 1000) x = 1000;
    if (x < -1000) x = -1000;

    x = x * 450 / 1000;

    if (x > 450) x = 450;
    if (x < -450) x = -450;

    return (int16_t)x;
}

void RC_Parse181(RcData_t* rc, const uint8_t data[8])
{
    rc->lx = (int16_t)((data[0] << 8) | data[1]);
    rc->ly = (int16_t)((data[2] << 8) | data[3]);
    rc->rx = (int16_t)((data[4] << 8) | data[5]);
    rc->ry = (int16_t)((data[6] << 8) | data[7]);
}

void RC_Parse281(RcData_t* rc, const uint8_t data[8])
{
    uint8_t D0 = data[0];
    uint8_t D3 = data[3];

    rc->fun_main_page = (D0 >> 3) & 0x1;
    rc->fun_estop_ok = (D0 >> 4) & 0x1;
    rc->fun_rf_ok = (D0 >> 7) & 0x1;

    rc->D1 = data[1];

    rc->car_enable = (D3 >> 0) & 0x1;
    rc->car_horn = (D3 >> 1) & 0x1;
    rc->car_epb = (D3 >> 2) & 0x1;
    rc->car_auto_mode = (D3 >> 3) & 0x1;
    rc->car_fn1 = (D3 >> 4) & 0x1;
    rc->car_fn2 = (D3 >> 5) & 0x1;
    rc->car_normal_mode = (D3 >> 6) & 0x1;
    rc->car_super_mode = (D3 >> 7) & 0x1;
}

void RC_Build10B(const RcData_t* rc, uint8_t data[8])
{
    int16_t x1 = RC_Scale1000To450(rc->lx);
    int16_t y1 = RC_Scale1000To450(rc->ly);
    int16_t x2 = RC_Scale1000To450(rc->rx);
    int16_t y2 = RC_Scale1000To450(rc->ry);

    data[0] = (uint8_t)(x2 & 0xFF);
    data[1] = (uint8_t)((x2 >> 8) & 0xFF);
    data[2] = (uint8_t)(y2 & 0xFF);
    data[3] = (uint8_t)((y2 >> 8) & 0xFF);
    data[4] = (uint8_t)(y1 & 0xFF);
    data[5] = (uint8_t)((y1 >> 8) & 0xFF);
    data[6] = (uint8_t)(x1 & 0xFF);
    data[7] = (uint8_t)((x1 >> 8) & 0xFF);
}

void RC_Build10B_Standby(uint8_t data[8]) { memset(data, 0, 8); }

void RC_Build10A(const RcData_t* rc, uint8_t data[8])
{
    RemoteIO_t io = {0};

    if (rc->car_super_mode) { io.remote_e = REMOTE_E_MODE_SUPER; }
    else if (rc->car_normal_mode) { io.remote_e = (rc->D1 & 0x40) ? REMOTE_E_MODE_NORMAL : REMOTE_E_MODE_TAKEOVER; }
    else
    {
        io.remote_e = REMOTE_E_MODE_NORMAL;
    }

    io.remote_c = rc->car_enable ? REMOTE_C_PWR_ON : REMOTE_C_PWR_OFF;
    io.remote_d = rc->car_horn ? REMOTE_D_HORN_ON : REMOTE_D_HORN_OFF;
    io.remote_b = rc->car_epb ? REMOTE_B_EPB_APPLY : REMOTE_B_EPB_RELEASE;
    io.remote_a = rc->car_auto_mode ? REMOTE_A_MODE_AUTO : REMOTE_A_MODE_REMOTE;

    if ((rc->car_fn1 == 1) && (rc->car_fn2 == 0))
        io.remote_f = REMOTE_F_LIGHT_OFF;
    else if ((rc->car_fn1 == 0) && (rc->car_fn2 == 1))
        io.remote_f = REMOTE_F_LIGHT_ON;
    else
        io.remote_f = REMOTE_F_LIGHT_OFF;

    data[0] = io.remote_e;
    data[1] = io.remote_f;
    data[2] = io.remote_a;
    data[3] = io.remote_b;
    data[4] = io.remote_c;
    data[5] = io.remote_d;
    data[6] = 0x00;
    data[7] = 0x00;
}
