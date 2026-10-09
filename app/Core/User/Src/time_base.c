#include "time_base.h"

#include <stdio.h>
#include <string.h>

#include "main.h"

static uint32_t g_unix_sec = 0;   // 秒级时间（来自4G）
static uint32_t g_sync_tick = 0;  // 同步时刻 tick
static uint8_t g_valid = 0;

/* ===== 简单日期转 unix ===== */
/* 不考虑时区，直接按 UTC 处理 */
static uint32_t ymdhms_to_unix(int y, int mon, int d, int h, int m, int s)
{
    /* 简化版（够用） */
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    uint32_t days_total = 0;
    int i;

    for (i = 1970; i < y; i++) { days_total += ((i % 4 == 0 && i % 100 != 0) || (i % 400 == 0)) ? 366 : 365; }

    for (i = 1; i < mon; i++)
    {
        days_total += days[i - 1];
        if (i == 2)
        {
            if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) days_total += 1;
        }
    }

    days_total += (d - 1);

    return days_total * 86400 + h * 3600 + m * 60 + s;
}

void TimeBase_Init(void) { g_valid = 0; }

/*
 * 解析：
 * +CCLK: "25/03/25,11:20:30+32"
 */
void TimeBase_UpdateFromCCLK(const char* line)
{
    int yy, MM, dd, hh, mm, ss;

    if (!line) return;

    if (strstr(line, "+CCLK:") == 0) return;

    if (sscanf(line, "+CCLK: \"%2d/%2d/%2d,%2d:%2d:%2d", &yy, &MM, &dd, &hh, &mm, &ss) == 6)
    {
        int year = 2000 + yy;

        g_unix_sec = ymdhms_to_unix(year, MM, dd, hh, mm, ss);
        g_sync_tick = HAL_GetTick();
        g_valid = 1;
    }
}

uint64_t TimeBase_GetMs(void)
{
    if (!g_valid)
    {
        return (uint64_t)HAL_GetTick();  // fallback
    }

    uint32_t now = HAL_GetTick();
    uint32_t delta = now - g_sync_tick;

    return ((uint64_t)g_unix_sec * 1000ULL) + delta;
}

uint8_t TimeBase_FormatUtcMs(char* out, uint16_t out_sz)
{
    char reversed[20];
    uint64_t value;
    uint16_t digits = 0U;
    uint16_t i;

    if ((out == 0) || (out_sz == 0U)) return 0U;
    out[0] = '\0';

    value = TimeBase_GetMs();
    do
    {
        reversed[digits++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    } while ((value != 0ULL) && (digits < (uint16_t)sizeof(reversed)));

    if ((value != 0ULL) || ((uint16_t)(digits + 1U) > out_sz)) return 0U;

    for (i = 0U; i < digits; i++) { out[i] = reversed[digits - i - 1U]; }
    out[digits] = '\0';
    return 1U;
}

uint8_t TimeBase_IsValid(void) { return g_valid; }
