#include "relay485_test.h"

#include <stdio.h>
#include <string.h>

#include "gpio.h"
#include "main.h"
#include "relay485_protocol.h"
#include "usart.h"

#define RELAY485_POLL_BYTES_PER_TASK 32U
#define RELAY485_LINK_LED_HOLD_MS 100U

static Relay485Protocol_t g_protocol;
static uint32_t g_work_led_tick;
static uint32_t g_link_led_until;

static void relay485_set_output(void* context, uint8_t on)
{
    (void)context;
    HAL_GPIO_WritePin(RELAY_CTL_GPIO_Port, RELAY_CTL_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void relay485_send_line(void* context, const char* line)
{
    char wire_line[160];
    int length;

    (void)context;
    if (line == 0) return;
    length = snprintf(wire_line, sizeof(wire_line), "%s\r\n", line);
    if ((length <= 0) || (length >= (int)sizeof(wire_line))) return;

    (void)HAL_UART_Transmit(&huart5, (uint8_t*)wire_line, (uint16_t)length, 200U);
}

void Relay485Test_Init(void)
{
    const Relay485Io_t io = { relay485_set_output, relay485_send_line, 0 };

    Relay485Protocol_Init(&g_protocol, &io);
    /* Keep unrelated CAN transceivers quiet during the relay-only test. */
    HAL_GPIO_WritePin(CAN1_STB_GPIO_Port, CAN1_STB_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(CAN2_STB_GPIO_Port, CAN2_STB_Pin, GPIO_PIN_SET);
    relay485_set_output(0, 0U);
    /* Diagnostic LEDs: COMM=firmware entered, WORK=main-loop heartbeat. */
    HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_3_GPIO_Port, LED_3_Pin, GPIO_PIN_SET);
    g_work_led_tick = HAL_GetTick();
    g_link_led_until = 0U;
}

static void relay485_poll_rx(uint32_t now)
{
    uint8_t byte;
    uint32_t count = 0U;

    while (count < RELAY485_POLL_BYTES_PER_TASK)
    {
        /* Timeout 0 makes this a non-blocking RXNE poll, matching the known-good test app. */
        if (HAL_UART_Receive(&huart5, &byte, 1U, 0U) != HAL_OK) break;

        Relay485Protocol_Feed(&g_protocol, &byte, 1U, now);
        HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_SET);
        g_link_led_until = now + RELAY485_LINK_LED_HOLD_MS;
        count++;
    }
}

void Relay485Test_Task(void)
{
    const uint32_t now = HAL_GetTick();

    if ((uint32_t)(now - g_work_led_tick) >= 500U)
    {
        g_work_led_tick = now;
        HAL_GPIO_TogglePin(LED_3_GPIO_Port, LED_3_Pin);
    }

    relay485_poll_rx(now);
    if ((int32_t)(now - g_link_led_until) >= 0)
        HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_RESET);

    Relay485Protocol_Task(&g_protocol, HAL_GetTick());
}
