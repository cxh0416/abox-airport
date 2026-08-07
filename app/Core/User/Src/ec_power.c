#include "ec_power.h"

#include "gpio.h"

#define EC_POWER_OFF_HOLD_MS 6000U   // 彻底断电保持时间，建议 6000~8000
#define EC_POWER_ON_STABLE_MS 2000U  // 3.8V 上电后等待稳定
#define EC_POWER_KEY_HOLD_MS 3000U   // PWRKEY 按住时间

typedef enum
{
    EC_PWR_IDLE = 0,

    EC_PWR_WAIT_OFF_HOLD,
    EC_PWR_WAIT_ON_STABLE,
    EC_PWR_WAIT_KEY_HOLD,

    EC_PWR_DONE
} EC_PowerState_t;

static EC_PowerState_t g_ec_pwr_state = EC_PWR_IDLE;
static uint32_t g_ec_pwr_ts = 0;

static void EC_Power_OutputOff(void)
{
    HAL_GPIO_WritePin(EC_PWR_GPIO_Port, EC_PWR_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EN_3_8V_GPIO_Port, EN_3_8V_Pin, GPIO_PIN_RESET);
}

void EC_Power_Init(void)
{
    g_ec_pwr_state = EC_PWR_IDLE;
    g_ec_pwr_ts = 0;
    EC_Power_OutputOff();
}

void EC_Power_StartOnSequence(void)
{
    EC_Power_OutputOff();

    g_ec_pwr_ts = HAL_GetTick();
    g_ec_pwr_state = EC_PWR_WAIT_OFF_HOLD;
}

void EC_Power_StartRestartSequence(void)
{
    EC_Power_OutputOff();

    g_ec_pwr_ts = HAL_GetTick();
    g_ec_pwr_state = EC_PWR_WAIT_OFF_HOLD;
}

void EC_Power_Task(void)
{
    uint32_t now = HAL_GetTick();

    switch (g_ec_pwr_state)
    {
        case EC_PWR_IDLE:
        case EC_PWR_DONE: break;

        case EC_PWR_WAIT_OFF_HOLD:
            if ((now - g_ec_pwr_ts) >= EC_POWER_OFF_HOLD_MS)
            {
                HAL_GPIO_WritePin(EN_3_8V_GPIO_Port, EN_3_8V_Pin, GPIO_PIN_SET);
                g_ec_pwr_ts = now;
                g_ec_pwr_state = EC_PWR_WAIT_ON_STABLE;
            }
            break;

        case EC_PWR_WAIT_ON_STABLE:
            if ((now - g_ec_pwr_ts) >= EC_POWER_ON_STABLE_MS)
            {
                HAL_GPIO_WritePin(EC_PWR_GPIO_Port, EC_PWR_Pin, GPIO_PIN_SET);
                g_ec_pwr_ts = now;
                g_ec_pwr_state = EC_PWR_WAIT_KEY_HOLD;
            }
            break;

        case EC_PWR_WAIT_KEY_HOLD:
            if ((now - g_ec_pwr_ts) >= EC_POWER_KEY_HOLD_MS)
            {
                HAL_GPIO_WritePin(EC_PWR_GPIO_Port, EC_PWR_Pin, GPIO_PIN_RESET);
                g_ec_pwr_state = EC_PWR_DONE;
            }
            break;

        default: g_ec_pwr_state = EC_PWR_IDLE; break;
    }
}

uint8_t EC_Power_IsBusy(void) { return (g_ec_pwr_state != EC_PWR_IDLE && g_ec_pwr_state != EC_PWR_DONE); }

uint8_t EC_Power_IsOnDone(void) { return (g_ec_pwr_state == EC_PWR_DONE); }