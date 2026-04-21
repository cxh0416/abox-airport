#include "bsp_can.h"

HAL_StatusTypeDef BSP_CAN_SendStd(CAN_HandleTypeDef* hcan, uint16_t stdId, const uint8_t* data, uint8_t dlc)
{
    CAN_TxHeaderTypeDef txHeader;
    uint32_t mailbox;

    if (dlc > 8) dlc = 8;

    txHeader.StdId = stdId;
    txHeader.ExtId = 0;
    txHeader.IDE = CAN_ID_STD;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.DLC = dlc;
    txHeader.TransmitGlobalTime = DISABLE;

    return HAL_CAN_AddTxMessage(hcan, &txHeader, (uint8_t*)data, &mailbox);
}
