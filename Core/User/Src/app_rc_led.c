#include "app_rc_priv.h"
#include "gpio.h"
#include "main.h"

static uint32_t s_led1_tick = 0;
static uint32_t s_led2_tick = 0;
static uint32_t s_led3_tick = 0;
static uint8_t s_led2_state = 0;
static uint8_t s_led3_state = 0;

void AppRcLed_Init(uint32_t now)
{
    s_led1_tick = now;
    s_led2_tick = now;
    s_led3_tick = now;
    s_led2_state = 0;
    s_led3_state = 0;

    HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_3_GPIO_Port, LED_3_Pin, GPIO_PIN_RESET);
}

void AppRcLed_Process(uint32_t now, uint8_t source_ok, uint8_t estop_active)
{
    uint8_t can1_alive = 0;

    if (((now - g_rc_app.last_181_tick) < 500U) || ((now - g_rc_app.last_281_tick) < 500U)) { can1_alive = 1U; }

    if (can1_alive)
    {
        if ((now - s_led1_tick) >= 500U)
        {
            s_led1_tick = now;
            HAL_GPIO_TogglePin(LED_1_GPIO_Port, LED_1_Pin);
        }
    }
    else
    {
        HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_RESET);
    }

    if (source_ok && (!estop_active))
    {
        if ((now - s_led2_tick) >= 500U)
        {
            s_led2_tick = now;
            s_led2_state = !s_led2_state;
            HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, s_led2_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }
    else
    {
        s_led2_state = 0;
        HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_RESET);
    }

    if ((!source_ok) || estop_active)
    {
        s_led3_state = 0;
        HAL_GPIO_WritePin(LED_3_GPIO_Port, LED_3_Pin, GPIO_PIN_RESET);
    }
    else if (g_rc_app.standby_mode)
    {
        if ((now - s_led3_tick) >= 300U)
        {
            s_led3_tick = now;
            s_led3_state = !s_led3_state;
            HAL_GPIO_WritePin(LED_3_GPIO_Port, LED_3_Pin, s_led3_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }
    else
    {
        s_led3_state = 1;
        HAL_GPIO_WritePin(LED_3_GPIO_Port, LED_3_Pin, GPIO_PIN_SET);
    }
}
