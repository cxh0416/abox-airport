/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

#include "can.h"
#include "dma.h"
#include "gpio.h"
#include "usart.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_rc.h"
#include "abox_product_port.h"
#include "boot_cfg.h"
#include "ec_power.h"
#include "led_message.h"
#include "motor485.h"
#include "mqtt_app.h"
#include "mqtt_core.h"
#include "serial_bus.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static MQTT_CoreConfig_t g_mqtt_cfg = {0};

#define UART1_RX_BUF_SZ 512
uint8_t uart1_rx_buf[UART1_RX_BUF_SZ];

#define UART5_RX_BUF_SZ 256
uint8_t uart5_rx_buf[UART5_RX_BUF_SZ];

static uint8_t g_mqtt_start_enable = 0;
static uint32_t g_ec_restart_tick = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int fputc(int ch, FILE* f)
{
    (void)ch;
    (void)f;
    return ch;
}

static char g_client_id[32];

static void GenClientId(void)
{
    uint32_t uid0 = *(uint32_t*)0x1FFFF7E8;
    uint32_t uid1 = *(uint32_t*)0x1FFFF7EC;
    uint32_t uid2 = *(uint32_t*)0x1FFFF7F0;

    snprintf(g_client_id, sizeof(g_client_id), "dev_%08" PRIX32 "%08" PRIX32 "%08" PRIX32, uid0, uid1, uid2);
}

static void MQTT_ConfigInit(void)
{
    memset(&g_mqtt_cfg, 0, sizeof(g_mqtt_cfg));

    g_mqtt_cfg.client_idx = 0;

    strncpy(g_mqtt_cfg.server_host, "36.153.162.171", sizeof(g_mqtt_cfg.server_host) - 1);
    g_mqtt_cfg.server_port = 61883;

    strncpy(g_mqtt_cfg.client_id, g_client_id, sizeof(g_mqtt_cfg.client_id) - 1);
    strncpy(g_mqtt_cfg.username, "zxwl", sizeof(g_mqtt_cfg.username) - 1);
    strncpy(g_mqtt_cfg.password, "zxwl1234@", sizeof(g_mqtt_cfg.password) - 1);

    g_mqtt_cfg.sub_topic[0] = '\0';
    g_mqtt_cfg.pub_topic[0] = '\0';
}
/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{
    /* USER CODE BEGIN 1 */
    SCB->VTOR = APP_START_ADDR;
    __DSB();
    __ISB();
    // __enable_irq();
    /* USER CODE END 1 */

    /* MCU Configuration--------------------------------------------------------*/

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* USER CODE BEGIN Init */
    __enable_irq();
    /* USER CODE END Init */

    /* Configure the system clock */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_CAN1_Init();
    MX_CAN2_Init();
    MX_USART1_UART_Init();
    MX_UART5_Init();
    /* USER CODE BEGIN 2 */

    ABox_ProductPlatform_Init();

    HAL_GPIO_WritePin(PVIN_OUT2_GPIO_Port, PVIN_OUT2_Pin, GPIO_PIN_SET);

    // 使能CAN
    HAL_GPIO_WritePin(CAN1_STB_GPIO_Port, CAN1_STB_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CAN2_STB_GPIO_Port, CAN2_STB_Pin, GPIO_PIN_RESET);

    EC_Power_Init();
    EC_Power_StartRestartSequence();
    g_ec_restart_tick = HAL_GetTick();
    g_mqtt_start_enable = 0;

    HAL_UART_Transmit(&huart5, (uint8_t*)"[APP] start ec restart\r\n", 24, 1000);

    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, uart1_rx_buf, UART1_RX_BUF_SZ);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);

    HAL_UARTEx_ReceiveToIdle_DMA(&huart5, uart5_rx_buf, UART5_RX_BUF_SZ);
    __HAL_DMA_DISABLE_IT(huart5.hdmarx, DMA_IT_HT);

    SerialBus_Init();
    SerialBus_RegisterLedRxHandler(LedMessage_OnBusRx);
    SerialBus_RegisterMotorRxHandler(Motor485_OnRxFrame);

    Motor485_Init();
    AppRc_Init(HAL_GetTick());
    LedMessage_Init();

    GenClientId();
    MQTT_ConfigInit();

    MQTT_Core_Init(&g_mqtt_cfg, MQTT_App_OnMessage);

    MQTT_App_Init("V000002");
    MQTT_Core_SetTopics(MQTT_App_GetCtrlTopic(), MQTT_App_GetHeartbeatTopic());

    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        static uint32_t last_tick_5ms = 0;
        uint32_t now = HAL_GetTick();

        if (now - last_tick_5ms >= 5)
        {
            last_tick_5ms = now;
            AppRc_Proc_5ms(now);
        }

        EC_Power_Task();

        if ((!g_mqtt_start_enable) && ((HAL_GetTick() - g_ec_restart_tick) >= 8000U))
        {
            g_mqtt_start_enable = 1;
            // HAL_UART_Transmit(&huart5, (uint8_t*)"[APP] mqtt start enable\r\n", 25, 1000);
        }

        if (g_mqtt_start_enable)
        {
            MQTT_Core_Task();
            MQTT_App_Task();
        }

        Motor485_Task(now);
        LedMessage_Task();
        SerialBus_Task();
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
    }
    /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.Prediv1Source = RCC_PREDIV1_SOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    RCC_OscInitStruct.PLL2.PLL2State = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }

    /** Configure the Systick interrupt time
     */
    __HAL_RCC_PLLI2S_ENABLE();
}

/* USER CODE BEGIN 4 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan)
{
    CAN_RxHeaderTypeDef rxHeader;
    uint8_t rxData[8];

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rxHeader, rxData) != HAL_OK) return;

    AppRc_OnCanRx(hcan, &rxHeader, rxData);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size)
{
    if (huart->Instance == USART1)
    {
        MQTT_Core_OnUartRx(uart1_rx_buf, Size);

        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, uart1_rx_buf, UART1_RX_BUF_SZ);
        __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    }
    else if (huart->Instance == UART5)
    {
        SerialBus_OnRxFrame(uart5_rx_buf, Size);

        HAL_UARTEx_ReceiveToIdle_DMA(&huart5, uart5_rx_buf, UART5_RX_BUF_SZ);
        __HAL_DMA_DISABLE_IT(huart5.hdmarx, DMA_IT_HT);
    }
}
/* USER CODE END 4 */

/**
 * @brief  Period elapsed callback in non blocking mode
 * @note   This function is called  when TIM1 interrupt took place, inside
 * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
 * a global variable "uwTick" used as application time base.
 * @param  htim : TIM handle
 * @retval None
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim)
{
    /* USER CODE BEGIN Callback 0 */

    /* USER CODE END Callback 0 */
    if (htim->Instance == TIM1) { HAL_IncTick(); }
    /* USER CODE BEGIN Callback 1 */

    /* USER CODE END Callback 1 */
}

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1) {}
    /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t* file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
