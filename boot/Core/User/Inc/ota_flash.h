#ifndef __OTA_FLASH_H__
#define __OTA_FLASH_H__

#include <stdint.h>

uint8_t OTA_Flash_Begin(void);
uint8_t OTA_Flash_Write(const uint8_t* data, uint16_t len);
void OTA_Flash_End(void);
void OTA_Flash_Abort(void);
uint32_t OTA_Flash_GetWrittenSize(void);

#endif