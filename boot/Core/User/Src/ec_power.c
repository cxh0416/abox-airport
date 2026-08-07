#include "ec_power.h"

#include "gpio.h"

#define EC_PWRKEY_GPIO_Port EC_PWR_GPIO_Port
#define EC_PWRKEY_Pin EC_PWR_Pin

typedef enum
{
    EC_PWR_IDLE = 0,

    EC_PWR_ON_WAIT_OFF_1S,
    EC_PWR_ON_WAIT_ON_1S,
    EC_PWR_ON_WAIT_KEY_2S,

    EC_PWR_RESTART_WAIT_OFF_3S,

    EC_PWR_DONE
} EC_PowerState_t;

static EC_PowerState_t g_ec_pwr_state = EC_PWR_IDLE;
static uint32_t g_ec_pwr_ts = 0;

void EC_Power_Init(void)
{
    g_ec_pwr_state = EC_PWR_IDLE;
    g_ec_pwr_ts = 0;
}

void EC_Power_ForceOff(void)
{
    HAL_GPIO_WritePin(EC_PWRKEY_GPIO_Port, EC_PWRKEY_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EN_3_8V_GPIO_Port, EN_3_8V_Pin, GPIO_PIN_RESET);

    g_ec_pwr_state = EC_PWR_IDLE;
    g_ec_pwr_ts = HAL_GetTick();
}

void EC_Power_StartOnSequence(void)
{
    HAL_GPIO_WritePin(EC_PWRKEY_GPIO_Port, EC_PWRKEY_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EN_3_8V_GPIO_Port, EN_3_8V_Pin, GPIO_PIN_RESET);

    g_ec_pwr_ts = HAL_GetTick();
    g_ec_pwr_state = EC_PWR_ON_WAIT_OFF_1S;
}

void EC_Power_StartRestartSequence(void)
{
    HAL_GPIO_WritePin(EC_PWRKEY_GPIO_Port, EC_PWRKEY_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EN_3_8V_GPIO_Port, EN_3_8V_Pin, GPIO_PIN_RESET);

    g_ec_pwr_ts = HAL_GetTick();
    g_ec_pwr_state = EC_PWR_RESTART_WAIT_OFF_3S;
}

void EC_Power_Task(void)
{
    uint32_t now = HAL_GetTick();

    switch (g_ec_pwr_state)
    {
        case EC_PWR_IDLE:
        case EC_PWR_DONE: break;

        case EC_PWR_ON_WAIT_OFF_1S:
            if ((now - g_ec_pwr_ts) >= 1000U)
            {
                HAL_GPIO_WritePin(EN_3_8V_GPIO_Port, EN_3_8V_Pin, GPIO_PIN_SET);
                g_ec_pwr_ts = now;
                g_ec_pwr_state = EC_PWR_ON_WAIT_ON_1S;
            }
            break;

        case EC_PWR_ON_WAIT_ON_1S:
            if ((now - g_ec_pwr_ts) >= 1000U)
            {
                HAL_GPIO_WritePin(EC_PWRKEY_GPIO_Port, EC_PWRKEY_Pin, GPIO_PIN_SET);
                g_ec_pwr_ts = now;
                g_ec_pwr_state = EC_PWR_ON_WAIT_KEY_2S;
            }
            break;

        case EC_PWR_ON_WAIT_KEY_2S:
            if ((now - g_ec_pwr_ts) >= 2000U)
            {
                HAL_GPIO_WritePin(EC_PWRKEY_GPIO_Port, EC_PWRKEY_Pin, GPIO_PIN_RESET);
                g_ec_pwr_state = EC_PWR_DONE;
            }
            break;

        case EC_PWR_RESTART_WAIT_OFF_3S:
            if ((now - g_ec_pwr_ts) >= 3000U)
            {
                /* 断电 3 秒后，继续正常上电 */
                g_ec_pwr_ts = now;
                g_ec_pwr_state = EC_PWR_ON_WAIT_OFF_1S;
            }
            break;

        default: g_ec_pwr_state = EC_PWR_IDLE; break;
    }
}

uint8_t EC_Power_IsBusy(void) { return (g_ec_pwr_state != EC_PWR_IDLE && g_ec_pwr_state != EC_PWR_DONE); }

uint8_t EC_Power_IsOnDone(void) { return (g_ec_pwr_state == EC_PWR_DONE); }

uint8_t EC_Power_IsDone(void) { return (g_ec_pwr_state == EC_PWR_DONE); }