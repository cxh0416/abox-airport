#ifndef SERIAL_BUS_H
#define SERIAL_BUS_H

#include <stdint.h>

typedef enum
{
    SERIAL_BUS_DEV_UNKNOWN = 0,
    SERIAL_BUS_DEV_LED = 1,
    SERIAL_BUS_DEV_MOTOR = 2
} SerialBusDev_t;

typedef void (*SerialBusRxHandler_t)(const uint8_t* data, uint16_t len);

void SerialBus_Init(void);
void SerialBus_Task(void);

/* 发送入队 */
int SerialBus_QueueTx(SerialBusDev_t dev, const uint8_t* data, uint16_t len);

/* 接收分发：UART5收到一帧后喂进来 */
void SerialBus_OnRxFrame(const uint8_t* data, uint16_t len);

/* 注册各业务接收处理函数 */
void SerialBus_RegisterLedRxHandler(SerialBusRxHandler_t handler);
void SerialBus_RegisterMotorRxHandler(SerialBusRxHandler_t handler);

#endif