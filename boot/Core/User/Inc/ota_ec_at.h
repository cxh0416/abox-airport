#ifndef __OTA_EC_AT_H__
#define __OTA_EC_AT_H__

#include <stdint.h>

#include "main.h"

void OTA_EC_AT_Init(void);
void OTA_EC_AT_Task(void);

void OTA_EC_AT_OnRx(uint8_t* data, uint16_t len);

void OTA_EC_SendAT(const char* cmd);
void OTA_EC_SendAT_F(const char* fmt, ...);

uint8_t OTA_EC_TestAT(void);
uint8_t OTA_EC_IsReady(void);

/* SIM */
void OTA_EC_RequestCpin(void);
uint8_t OTA_EC_IsSimReady(void);
uint8_t OTA_EC_IsCpinDone(void);

/* CSQ */
void OTA_EC_RequestCsq(void);
uint8_t OTA_EC_IsCsqDone(void);
int OTA_EC_GetCsq(void);

/* CEREG */
void OTA_EC_RequestCereg(void);
void OTA_EC_RequestEchoOff(void);
uint8_t OTA_EC_IsCeregDone(void);
uint8_t OTA_EC_IsNetReady(void);
int OTA_EC_GetCeregStat(void);

/* ERROR */
uint8_t OTA_EC_HasCmeError(void);
int OTA_EC_GetCmeErrorCode(void);
void OTA_EC_ClearErrors(void);

void OTA_EC_RegisterLineHook(void (*hook)(const char* line));
void OTA_EC_RegisterRawHook(void (*hook)(const uint8_t* data, uint16_t len));

uint8_t OTA_EC_IsEchoOffDone(void);

#endif