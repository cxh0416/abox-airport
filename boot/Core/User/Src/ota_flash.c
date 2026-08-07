#include "ota_flash.h"

#include <string.h>

#include "boot_cfg.h"
#include "boot_dbg.h"
#include "main.h"

static uint32_t g_write_addr = APP_START_ADDR;
static uint8_t g_flash_started = 0;

/* 跨多次 Write 保留的尾巴字节 */
static uint8_t g_pending[4];
static uint8_t g_pending_len = 0;

static uint8_t OTA_Flash_ProgramWordBytes(const uint8_t bytes[4])
{
    uint32_t word;

    if (g_write_addr >= OTA_INFO_ADDR)
    {
        boot_printf("[FLASH] overflow addr=0x%08lX\r\n", g_write_addr);
        HAL_FLASH_Lock();
        g_flash_started = 0;
        return 0;
    }

    word =
        ((uint32_t)bytes[0] << 0) | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, g_write_addr, word) != HAL_OK)
    {
        boot_printf("[FLASH] write fail addr=0x%08lX\r\n", g_write_addr);
        HAL_FLASH_Lock();
        g_flash_started = 0;
        return 0;
    }

    g_write_addr += 4U;
    return 1;
}

uint8_t OTA_Flash_Begin(void)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0;
    uint32_t page_count;

    if (g_flash_started)
    {
        HAL_FLASH_Lock();
        g_flash_started = 0;
    }

    HAL_FLASH_Unlock();

    page_count = (APP_MAX_SIZE + OTA_FLASH_PAGE_SIZE - 1U) / OTA_FLASH_PAGE_SIZE;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = APP_START_ADDR;
    erase.NbPages = page_count;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        boot_printf("[FLASH] erase fail page=0x%08lX\r\n", page_error);
        HAL_FLASH_Lock();
        g_flash_started = 0;
        g_write_addr = APP_START_ADDR;
        g_pending_len = 0;
        return 0;
    }

    g_write_addr = APP_START_ADDR;
    g_flash_started = 1;
    g_pending_len = 0;
    memset(g_pending, 0xFF, sizeof(g_pending));

    boot_printf("[FLASH] erase ok, start=0x%08lX, pages=%lu\r\n", APP_START_ADDR, page_count);
    return 1;
}

uint8_t OTA_Flash_Write(const uint8_t* data, uint16_t len)
{
    uint16_t i;

    if ((data == 0) || (len == 0)) return 1;
    if (!g_flash_started) return 0;

    for (i = 0; i < len; i++)
    {
        g_pending[g_pending_len++] = data[i];

        if (g_pending_len >= 4U)
        {
            if (!OTA_Flash_ProgramWordBytes(g_pending)) { return 0; }

            g_pending_len = 0;
            memset(g_pending, 0xFF, sizeof(g_pending));
        }
    }

    return 1;
}

void OTA_Flash_End(void)
{
    if (g_flash_started)
    {
        /* 只允许在最终结束时，对最后不足 4 字节的尾巴补 FF */
        if (g_pending_len > 0U)
        {
            while (g_pending_len < 4U) { g_pending[g_pending_len++] = 0xFFU; }

            if (!OTA_Flash_ProgramWordBytes(g_pending))
            {
                /* Program 失败时，内部已经打日志并关锁了 */
                g_pending_len = 0;
                memset(g_pending, 0xFF, sizeof(g_pending));
                return;
            }
        }

        HAL_FLASH_Lock();
        boot_printf("[FLASH] write done, end=0x%08lX\r\n", g_write_addr);
    }

    g_flash_started = 0;
    g_pending_len = 0;
    memset(g_pending, 0xFF, sizeof(g_pending));
}

void OTA_Flash_Abort(void)
{
    if (g_flash_started)
    {
        HAL_FLASH_Lock();
        boot_printf("[FLASH] abort, end=0x%08lX\r\n", g_write_addr);
    }

    g_flash_started = 0;
    g_pending_len = 0;
    memset(g_pending, 0xFF, sizeof(g_pending));
}

uint32_t OTA_Flash_GetWrittenSize(void)
{
    if (g_write_addr < APP_START_ADDR) return 0;
    return (g_write_addr - APP_START_ADDR);
}