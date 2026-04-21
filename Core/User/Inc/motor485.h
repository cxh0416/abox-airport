#ifndef MOTOR485_H
#define MOTOR485_H

#include <stdint.h>

typedef enum
{
    MOTOR_CMD_STOP = 0,
    MOTOR_CMD_FWD = 1,
    MOTOR_CMD_REV = 2
} MotorCmd_t;

void Motor485_Init(void);
void Motor485_Task(uint32_t now);

/* 上层只提请求，不直接发串口 */
void Motor485_RequestSet(uint8_t slave, MotorCmd_t cmd);

/* 串口收到一帧后喂给它 */
void Motor485_OnRxFrame(const uint8_t* data, uint16_t len);

/* 急停时一键全停 */
void Motor485_RequestStopAll(void);

#endif