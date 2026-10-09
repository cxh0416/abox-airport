#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "relay485_protocol.h"

typedef struct
{
    uint8_t output_on;
    char responses[16][160];
    size_t response_count;
} TestContext;

static void test_set_output(void* context, uint8_t on)
{
    ((TestContext*)context)->output_on = on;
}

static void test_send_line(void* context, const char* line)
{
    TestContext* test = (TestContext*)context;
    assert(test->response_count < 16U);
    snprintf(test->responses[test->response_count], sizeof(test->responses[0]), "%s", line);
    test->response_count++;
}

static void reset_responses(TestContext* test)
{
    test->response_count = 0U;
    memset(test->responses, 0, sizeof(test->responses));
}

static Relay485Protocol_t make_protocol(TestContext* test)
{
    Relay485Protocol_t protocol;
    const Relay485Io_t io = { test_set_output, test_send_line, test };
    Relay485Protocol_Init(&protocol, &io);
    return protocol;
}

static void test_fragmented_and_pipelined_commands(void)
{
    TestContext test = {0};
    Relay485Protocol_t protocol = make_protocol(&test);

    Relay485Protocol_Feed(&protocol, (const uint8_t*)"O", 1U, 0U);
    assert(test.response_count == 0U);
    Relay485Protocol_Feed(&protocol, (const uint8_t*)"N\r\nSTATUS\n", 10U, 0U);
    assert(test.response_count == 2U);
    assert(strcmp(test.responses[0], "OK ON RELAY_CMD=ON MODE=IDLE REMAIN_MS=0") == 0);
    assert(strstr(test.responses[1], "OK STATUS RELAY_CMD=ON MODE=IDLE") == test.responses[1]);
    assert(test.output_on == 1U);
}

static void test_invalid_and_overlong_commands(void)
{
    TestContext test = {0};
    Relay485Protocol_t protocol = make_protocol(&test);
    char overlong[RELAY485_LINE_MAX + 8U];

    Relay485Protocol_Feed(&protocol, (const uint8_t*)"NOPE\n", 5U, 0U);
    assert(strcmp(test.responses[0], "ERR BAD_CMD") == 0);
    reset_responses(&test);
    Relay485Protocol_Feed(&protocol, (const uint8_t*)"CYCLE 99\n", 9U, 0U);
    assert(strcmp(test.responses[0], "ERR BAD_ARG") == 0);
    reset_responses(&test);
    memset(overlong, 'X', sizeof(overlong));
    overlong[sizeof(overlong) - 1U] = '\n';
    Relay485Protocol_Feed(&protocol, (const uint8_t*)overlong, sizeof(overlong), 0U);
    assert(strcmp(test.responses[0], "ERR LINE_TOO_LONG") == 0);
}

static void test_cycle_busy_cancel_and_wraparound(void)
{
    TestContext test = {0};
    Relay485Protocol_t protocol = make_protocol(&test);
    const uint32_t start = UINT32_MAX - 10U;

    Relay485Protocol_Feed(&protocol, (const uint8_t*)"CYCLE 100\n", 10U, start);
    assert(test.output_on == 0U);
    assert(Relay485Protocol_GetMode(&protocol) == RELAY485_MODE_CYCLE);
    reset_responses(&test);
    Relay485Protocol_Feed(&protocol, (const uint8_t*)"CYCLE 100\n", 10U, start + 1U);
    assert(strcmp(test.responses[0], "ERR BUSY") == 0);
    reset_responses(&test);
    Relay485Protocol_Task(&protocol, start + 99U);
    assert(test.response_count == 0U);
    Relay485Protocol_Task(&protocol, start + 100U);
    assert(test.output_on == 1U);
    assert(strcmp(test.responses[0], "EVENT CYCLE_DONE RELAY_CMD=ON") == 0);

    reset_responses(&test);
    Relay485Protocol_Feed(&protocol, (const uint8_t*)"CYCLE 1000\n", 11U, 500U);
    Relay485Protocol_Feed(&protocol, (const uint8_t*)"OFF\n", 4U, 501U);
    assert(Relay485Protocol_GetMode(&protocol) == RELAY485_MODE_IDLE);
    assert(test.output_on == 0U);
    Relay485Protocol_Task(&protocol, 2000U);
    assert(test.response_count == 2U);
}

int main(void)
{
    test_fragmented_and_pipelined_commands();
    test_invalid_and_overlong_commands();
    test_cycle_busy_cancel_and_wraparound();
    puts("relay485 protocol tests passed");
    return 0;
}
