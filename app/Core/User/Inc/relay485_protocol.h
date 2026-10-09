#ifndef RELAY485_PROTOCOL_H
#define RELAY485_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RELAY485_LINE_MAX 64U
#define RELAY485_MIN_CYCLE_MS 100U
#define RELAY485_MAX_CYCLE_MS 600000U

typedef enum
{
    RELAY485_MODE_IDLE = 0U,
    RELAY485_MODE_CYCLE = 1U
} Relay485Mode_t;

typedef void (*Relay485SetOutputFn)(void* context, uint8_t on);
typedef void (*Relay485SendLineFn)(void* context, const char* line);

typedef struct
{
    Relay485SetOutputFn set_output;
    Relay485SendLineFn send_line;
    void* context;
} Relay485Io_t;

typedef struct
{
    Relay485Io_t io;
    char line[RELAY485_LINE_MAX];
    uint16_t line_len;
    uint8_t line_overflow;
    uint8_t relay_on;
    Relay485Mode_t mode;
    uint32_t cycle_deadline;
    uint32_t cycle_off_ms;
} Relay485Protocol_t;

void Relay485Protocol_Init(Relay485Protocol_t* protocol, const Relay485Io_t* io);
void Relay485Protocol_Feed(Relay485Protocol_t* protocol, const uint8_t* data, uint16_t len,
                           uint32_t now);
void Relay485Protocol_Task(Relay485Protocol_t* protocol, uint32_t now);
uint8_t Relay485Protocol_IsOn(const Relay485Protocol_t* protocol);
Relay485Mode_t Relay485Protocol_GetMode(const Relay485Protocol_t* protocol);
uint32_t Relay485Protocol_GetRemainingMs(const Relay485Protocol_t* protocol, uint32_t now);

#ifdef __cplusplus
}
#endif

#endif
