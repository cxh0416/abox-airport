#ifndef __OTA_INFO_H__
#define __OTA_INFO_H__

#include <stdint.h>

#include "boot_cfg.h"

typedef struct
{
    uint32_t ota_req_magic;
    uint32_t app_valid_magic;
    uint32_t fw_size;
    uint32_t fw_crc32;
    uint32_t req_fail_count;  // 请求URL失败次数
    char url[128];
    uint32_t reserved[21];
} OTA_Info_t;

void OTA_Info_Load(OTA_Info_t* info);
uint8_t OTA_Info_Save(const OTA_Info_t* info);
uint8_t OTA_Info_Clear(void);

uint8_t OTA_Info_HasRequest(void);
uint8_t OTA_Info_IsRequestValid(const OTA_Info_t* info);
uint8_t OTA_Info_AppMarkedValid(void);

void OTA_Info_MarkAppValid(void);
void OTA_Info_ClearAppValid(void);
void OTA_Info_SetRequest(uint32_t fw_size, uint32_t fw_crc32, const char* url);
void OTA_Info_ClearRequest(void);

uint32_t OTA_Info_GetReqFailCount(void);
void OTA_Info_ClearReqFailCount(void);
void OTA_Info_IncReqFailCount(void);

#endif
