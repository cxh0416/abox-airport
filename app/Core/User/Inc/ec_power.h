#ifndef __EC_POWER_H
#define __EC_POWER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "main.h"

    void EC_Power_Init(void);
    void EC_Power_StartOnSequence(void);
    void EC_Power_StartRestartSequence(void);
    void EC_Power_Task(void);
    uint8_t EC_Power_IsBusy(void);
    uint8_t EC_Power_IsOnDone(void);

#ifdef __cplusplus
}
#endif

#endif
