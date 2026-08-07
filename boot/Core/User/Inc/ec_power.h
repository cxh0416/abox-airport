#ifndef __EC_POWER_H__
#define __EC_POWER_H__

#include <stdint.h>

#include "main.h"

void EC_Power_Init(void);
void EC_Power_StartOnSequence(void);
void EC_Power_StartRestartSequence(void);
void EC_Power_Task(void);

void EC_Power_ForceOff(void);

uint8_t EC_Power_IsBusy(void);
uint8_t EC_Power_IsOnDone(void);
uint8_t EC_Power_IsDone(void);

#endif