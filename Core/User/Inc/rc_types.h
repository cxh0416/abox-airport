#ifndef __RC_TYPES_H__
#define __RC_TYPES_H__

#include "stdint.h"

typedef struct
{
    int16_t lx, ly;
    int16_t rx, ry;

    uint8_t fun_main_page;
    uint8_t fun_estop_ok;
    uint8_t fun_rf_ok;

    uint8_t D1;

    uint8_t car_enable;
    uint8_t car_horn;
    uint8_t car_epb;
    uint8_t car_auto_mode;
    uint8_t car_fn1;
    uint8_t car_fn2;
    uint8_t car_normal_mode;
    uint8_t car_super_mode;
} RcData_t;

typedef enum
{
    REMOTE_E_MODE_NORMAL = 0x00,
    REMOTE_E_MODE_TAKEOVER = 0x01,
    REMOTE_E_MODE_SUPER = 0x02,
} RemoteE_Mode_t;

typedef enum
{
    REMOTE_F_LIGHT_OFF = 0x00,
    REMOTE_F_LIGHT_ON = 0x01,
} RemoteF_Light_t;

typedef enum
{
    REMOTE_A_MODE_REMOTE = 0x00,
    REMOTE_A_MODE_AUTO = 0x02,
} RemoteA_Mode_t;

typedef enum
{
    REMOTE_B_EPB_RELEASE = 0x00,
    REMOTE_B_EPB_APPLY = 0x02,
} RemoteB_EPB_t;

typedef enum
{
    REMOTE_C_PWR_OFF = 0x00,
    REMOTE_C_PWR_ON = 0x02,
} RemoteC_Power_t;

typedef enum
{
    REMOTE_D_HORN_OFF = 0x00,
    REMOTE_D_HORN_ON = 0x02,
} RemoteD_Horn_t;

typedef struct
{
    uint8_t remote_e;
    uint8_t remote_f;
    uint8_t remote_a;
    uint8_t remote_b;
    uint8_t remote_c;
    uint8_t remote_d;
    uint8_t rsv6;
    uint8_t rsv7;
} RemoteIO_t;

#endif
