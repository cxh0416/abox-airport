#include "serial_bus.h"

#include <string.h>

#include "usart.h"

#define SERIAL_BUS_TX_Q_SIZE 16U
#define SERIAL_BUS_FRAME_MAX_LEN 512U

typedef struct
{
    uint8_t used;
    uint8_t dev;
    uint16_t len;
    uint8_t data[SERIAL_BUS_FRAME_MAX_LEN];
} SerialBusFrame_t;

static SerialBusFrame_t g_tx_q[SERIAL_BUS_TX_Q_SIZE];
static uint8_t g_tx_wr = 0;
static uint8_t g_tx_rd = 0;
static uint8_t g_tx_busy = 0;

/* 预留的业务接收处理 */
static SerialBusRxHandler_t g_led_rx_handler = 0;
static SerialBusRxHandler_t g_motor_rx_handler = 0;

static uint8_t SerialBus_QIsEmpty(void) { return (g_tx_wr == g_tx_rd); }

static uint8_t SerialBus_QIsFull(void) { return (((uint8_t)(g_tx_wr + 1U)) % SERIAL_BUS_TX_Q_SIZE) == g_tx_rd; }

void SerialBus_Init(void)
{
    memset(g_tx_q, 0, sizeof(g_tx_q));
    g_tx_wr = 0;
    g_tx_rd = 0;
    g_tx_busy = 0;

    g_led_rx_handler = 0;
    g_motor_rx_handler = 0;
}

void SerialBus_RegisterLedRxHandler(SerialBusRxHandler_t handler) { g_led_rx_handler = handler; }

void SerialBus_RegisterMotorRxHandler(SerialBusRxHandler_t handler) { g_motor_rx_handler = handler; }

int SerialBus_QueueTx(SerialBusDev_t dev, const uint8_t* data, uint16_t len)
{
    uint8_t idx;

    if ((data == 0) || (len == 0)) return 0;
    if (len > SERIAL_BUS_FRAME_MAX_LEN) return 0;
    if (SerialBus_QIsFull()) return 0;

    idx = g_tx_wr;

    g_tx_q[idx].used = 1U;
    g_tx_q[idx].dev = (uint8_t)dev;
    g_tx_q[idx].len = len;
    memcpy(g_tx_q[idx].data, data, len);

    g_tx_wr = (uint8_t)((g_tx_wr + 1U) % SERIAL_BUS_TX_Q_SIZE);
    return 1;
}

void SerialBus_Task(void)
{
    SerialBusFrame_t* f;

    if (g_tx_busy) return;
    if (SerialBus_QIsEmpty()) return;

    f = &g_tx_q[g_tx_rd];
    if (!f->used)
    {
        g_tx_rd = (uint8_t)((g_tx_rd + 1U) % SERIAL_BUS_TX_Q_SIZE);
        return;
    }

    g_tx_busy = 1U;

    if (HAL_UART_Transmit(&huart5, f->data, f->len, 1000) == HAL_OK)
    {
        f->used = 0U;
        g_tx_rd = (uint8_t)((g_tx_rd + 1U) % SERIAL_BUS_TX_Q_SIZE);
    }

    g_tx_busy = 0U;
}

void SerialBus_OnRxFrame(const uint8_t* data, uint16_t len)
{
    if ((data == 0) || (len == 0)) return;

    /*
     * 先做“广播式分发”：
     * - 现在 motor 用得上
     * - 后续 LED 要加接收，也能直接挂进来
     *
     * 业务层自己判断这帧是不是自己的
     */
    if (g_led_rx_handler) { g_led_rx_handler(data, len); }
    if (g_motor_rx_handler) { g_motor_rx_handler(data, len); }
}
