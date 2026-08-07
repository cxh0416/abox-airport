#ifndef __OTA_HTTP_H__
#define __OTA_HTTP_H__

#include <stdint.h>

void OTA_HTTP_Init(void);
void OTA_HTTP_OnLine(const char* line);
void OTA_HTTP_OnRawData(const uint8_t* data, uint16_t len);

void OTA_HTTP_Start(const char* url);
void OTA_HTTP_Task(void);

uint8_t OTA_HTTP_IsBusy(void);
uint8_t OTA_HTTP_IsDone(void);
uint8_t OTA_HTTP_HasError(void);

uint8_t OTA_HTTP_IsReadingRaw(void);

#endif