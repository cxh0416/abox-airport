#include "relay485_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t relay485_is_due(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0) ? 1U : 0U;
}

static void relay485_send(Relay485Protocol_t* protocol, const char* line)
{
    if ((protocol != 0) && (protocol->io.send_line != 0) && (line != 0))
    {
        protocol->io.send_line(protocol->io.context, line);
    }
}

static void relay485_set_output(Relay485Protocol_t* protocol, uint8_t on)
{
    if (protocol == 0) return;
    protocol->relay_on = on ? 1U : 0U;
    if (protocol->io.set_output != 0)
    {
        protocol->io.set_output(protocol->io.context, protocol->relay_on);
    }
}

static uint32_t relay485_remaining(const Relay485Protocol_t* protocol, uint32_t now)
{
    if ((protocol == 0) || (protocol->mode != RELAY485_MODE_CYCLE)) return 0U;
    if (relay485_is_due(now, protocol->cycle_deadline)) return 0U;
    return protocol->cycle_deadline - now;
}

static void relay485_trim(char* text)
{
    char* start;
    char* end;

    if (text == 0) return;
    start = text;
    while ((*start != '\0') && isspace((unsigned char)*start)) start++;
    if (start != text) memmove(text, start, strlen(start) + 1U);

    end = text + strlen(text);
    while ((end > text) && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
}

static uint8_t relay485_equals(const char* left, const char* right)
{
    if ((left == 0) || (right == 0)) return 0U;
    while ((*left != '\0') && (*right != '\0'))
    {
        if (toupper((unsigned char)*left) != toupper((unsigned char)*right)) return 0U;
        left++;
        right++;
    }
    return (*left == '\0') && (*right == '\0') ? 1U : 0U;
}

static uint8_t relay485_parse_cycle_ms(const char* text, uint32_t* out_ms)
{
    uint32_t value = 0U;
    const char* cursor = text;

    if ((cursor == 0) || (out_ms == 0)) return 0U;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '\0') return 0U;

    while (*cursor != '\0')
    {
        uint32_t digit;
        if ((*cursor < '0') || (*cursor > '9')) return 0U;
        digit = (uint32_t)(*cursor - '0');
        if (value > (UINT32_MAX - digit) / 10U) return 0U;
        value = value * 10U + digit;
        cursor++;
    }

    if ((value < RELAY485_MIN_CYCLE_MS) || (value > RELAY485_MAX_CYCLE_MS)) return 0U;
    *out_ms = value;
    return 1U;
}

static void relay485_status(Relay485Protocol_t* protocol, uint32_t now)
{
    char response[128];
    const uint32_t remaining = relay485_remaining(protocol, now);

    (void)snprintf(response, sizeof(response),
                   "OK STATUS RELAY_CMD=%s MODE=%s REMAIN_MS=%lu",
                   protocol->relay_on ? "ON" : "OFF",
                   protocol->mode == RELAY485_MODE_CYCLE ? "CYCLE" : "IDLE",
                   (unsigned long)remaining);
    relay485_send(protocol, response);
}

static void relay485_process_line(Relay485Protocol_t* protocol, uint32_t now)
{
    char* separator;
    char response[128];
    uint32_t off_ms;

    if (protocol == 0) return;
    protocol->line[protocol->line_len] = '\0';
    relay485_trim(protocol->line);

    if (relay485_equals(protocol->line, "ON"))
    {
        protocol->mode = RELAY485_MODE_IDLE;
        protocol->cycle_off_ms = 0U;
        relay485_set_output(protocol, 1U);
        relay485_send(protocol, "OK ON RELAY_CMD=ON MODE=IDLE REMAIN_MS=0");
        return;
    }

    if (relay485_equals(protocol->line, "OFF"))
    {
        protocol->mode = RELAY485_MODE_IDLE;
        protocol->cycle_off_ms = 0U;
        relay485_set_output(protocol, 0U);
        relay485_send(protocol, "OK OFF RELAY_CMD=OFF MODE=IDLE REMAIN_MS=0");
        return;
    }

    if (relay485_equals(protocol->line, "STATUS"))
    {
        relay485_status(protocol, now);
        return;
    }

    if ((strlen(protocol->line) >= 5U) &&
        (toupper((unsigned char)protocol->line[0]) == 'C') &&
        (toupper((unsigned char)protocol->line[1]) == 'Y') &&
        (toupper((unsigned char)protocol->line[2]) == 'C') &&
        (toupper((unsigned char)protocol->line[3]) == 'L') &&
        (toupper((unsigned char)protocol->line[4]) == 'E'))
    {
        separator = protocol->line + 5;
        if ((*separator != '\0') && !isspace((unsigned char)*separator))
        {
            relay485_send(protocol, "ERR BAD_ARG");
            return;
        }
        if (protocol->mode == RELAY485_MODE_CYCLE)
        {
            relay485_send(protocol, "ERR BUSY");
            return;
        }
        if (!relay485_parse_cycle_ms(separator, &off_ms))
        {
            relay485_send(protocol, "ERR BAD_ARG");
            return;
        }

        relay485_set_output(protocol, 0U);
        protocol->mode = RELAY485_MODE_CYCLE;
        protocol->cycle_off_ms = off_ms;
        protocol->cycle_deadline = now + off_ms;
        (void)snprintf(response, sizeof(response),
                       "OK CYCLE RELAY_CMD=OFF MODE=CYCLE REMAIN_MS=%lu",
                       (unsigned long)off_ms);
        relay485_send(protocol, response);
        return;
    }

    relay485_send(protocol, "ERR BAD_CMD");
}

void Relay485Protocol_Init(Relay485Protocol_t* protocol, const Relay485Io_t* io)
{
    if (protocol == 0) return;
    memset(protocol, 0, sizeof(*protocol));
    if (io != 0) protocol->io = *io;
}

void Relay485Protocol_Feed(Relay485Protocol_t* protocol, const uint8_t* data, uint16_t len,
                           uint32_t now)
{
    uint16_t index;

    if ((protocol == 0) || (data == 0)) return;
    for (index = 0U; index < len; index++)
    {
        const uint8_t byte = data[index];
        if (byte == '\r') continue;
        if (byte == '\n')
        {
            if (protocol->line_overflow)
            {
                relay485_send(protocol, "ERR LINE_TOO_LONG");
            }
            else
            {
                relay485_process_line(protocol, now);
            }
            protocol->line_len = 0U;
            protocol->line_overflow = 0U;
            protocol->line[0] = '\0';
            continue;
        }
        if (protocol->line_overflow) continue;
        if (protocol->line_len >= (RELAY485_LINE_MAX - 1U))
        {
            protocol->line_overflow = 1U;
            continue;
        }
        protocol->line[protocol->line_len++] = (char)byte;
    }
}

void Relay485Protocol_Task(Relay485Protocol_t* protocol, uint32_t now)
{
    if ((protocol == 0) || (protocol->mode != RELAY485_MODE_CYCLE)) return;
    if (!relay485_is_due(now, protocol->cycle_deadline)) return;

    protocol->mode = RELAY485_MODE_IDLE;
    protocol->cycle_off_ms = 0U;
    relay485_set_output(protocol, 1U);
    relay485_send(protocol, "EVENT CYCLE_DONE RELAY_CMD=ON");
}

uint8_t Relay485Protocol_IsOn(const Relay485Protocol_t* protocol)
{
    return (protocol != 0) ? protocol->relay_on : 0U;
}

Relay485Mode_t Relay485Protocol_GetMode(const Relay485Protocol_t* protocol)
{
    return (protocol != 0) ? protocol->mode : RELAY485_MODE_IDLE;
}

uint32_t Relay485Protocol_GetRemainingMs(const Relay485Protocol_t* protocol, uint32_t now)
{
    return relay485_remaining(protocol, now);
}
