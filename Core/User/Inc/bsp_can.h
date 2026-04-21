#ifndef __BSP_CAN_H__
#define __BSP_CAN_H__

#include <stdint.h>

#include "stm32f1xx_hal.h"

HAL_StatusTypeDef BSP_CAN_SendStd(CAN_HandleTypeDef* hcan, uint16_t stdId, const uint8_t* data, uint8_t dlc);

#endif
