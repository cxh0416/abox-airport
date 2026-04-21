#include "motor485.h"

#include <string.h>

#include "serial_bus.h"
#include "usart.h"

#define MOTOR_MAX_SLAVE 3U
#define MOTOR_REQ_Q_SIZE 16U
#define MOTOR_RSP_TIMEOUT_MS 200U
#define MOTOR_RETRY_MAX 2U

typedef enum
{
    MOTOR_SM_IDLE = 0,
    MOTOR_SM_SEND_WRITE,
    MOTOR_SM_WAIT_WRITE_RSP,
    MOTOR_SM_SEND_READ,
    MOTOR_SM_WAIT_READ_RSP
} MotorSmState_t;

typedef struct
{
    uint8_t used;
    uint8_t slave;
    uint16_t reg_addr;
    uint16_t value;
} MotorReq_t;

typedef struct
{
    MotorSmState_t sm;
    MotorReq_t cur_req;
    uint8_t waiting_slave;
    uint16_t waiting_value;
    uint8_t retry;
    uint32_t deadline;

    uint8_t rx_flag;
    uint8_t rx_buf[64];
    uint16_t rx_len;
} MotorCtx_t;

static MotorCtx_t g_motor;
static MotorReq_t g_q[MOTOR_REQ_Q_SIZE];
static uint8_t g_q_wr = 0;
static uint8_t g_q_rd = 0;

static uint16_t Modbus_CRC16(const uint8_t* buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t pos;
    uint8_t i;

    for (pos = 0; pos < len; pos++)
    {
        crc ^= buf[pos];
        for (i = 0; i < 8; i++)
        {
            if (crc & 0x0001)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static int Motor485_PortSend(const uint8_t* data, uint16_t len)
{ return SerialBus_QueueTx(SERIAL_BUS_DEV_MOTOR, data, len); }

static uint16_t Modbus_BuildWriteSingleReg(uint8_t slave, uint16_t reg, uint16_t value, uint8_t* buf)
{
    uint16_t crc;

    buf[0] = slave;
    buf[1] = 0x06;
    buf[2] = (uint8_t)(reg >> 8);
    buf[3] = (uint8_t)(reg & 0xFF);
    buf[4] = (uint8_t)(value >> 8);
    buf[5] = (uint8_t)(value & 0xFF);

    crc = Modbus_CRC16(buf, 6);
    buf[6] = (uint8_t)(crc & 0xFF);
    buf[7] = (uint8_t)(crc >> 8);

    return 8;
}

static uint16_t Modbus_BuildReadHoldingReg(uint8_t slave, uint16_t reg, uint16_t qty, uint8_t* buf)
{
    uint16_t crc;

    buf[0] = slave;
    buf[1] = 0x03;
    buf[2] = (uint8_t)(reg >> 8);
    buf[3] = (uint8_t)(reg & 0xFF);
    buf[4] = (uint8_t)(qty >> 8);
    buf[5] = (uint8_t)(qty & 0xFF);

    crc = Modbus_CRC16(buf, 6);
    buf[6] = (uint8_t)(crc & 0xFF);
    buf[7] = (uint8_t)(crc >> 8);

    return 8;
}

static int Modbus_CheckCrc(const uint8_t* buf, uint16_t len)
{
    uint16_t crc_calc;
    uint16_t crc_recv;

    if (len < 4) return 0;

    crc_calc = Modbus_CRC16(buf, len - 2);
    crc_recv = (uint16_t)buf[len - 2] | ((uint16_t)buf[len - 1] << 8);

    return (crc_calc == crc_recv);
}

static int Queue_IsEmpty(void) { return (g_q_wr == g_q_rd); }

static int Queue_IsFull(void) { return (((uint8_t)(g_q_wr + 1U)) % MOTOR_REQ_Q_SIZE) == g_q_rd; }

static int Queue_Push(uint8_t slave, uint16_t reg, uint16_t value)
{
    uint8_t i;
    uint8_t idx;

    if ((slave < 1U) || (slave > MOTOR_MAX_SLAVE)) return 0;

    /* 去重：相同站号/寄存器，只保留最新值 */
    for (i = g_q_rd; i != g_q_wr; i = (uint8_t)((i + 1U) % MOTOR_REQ_Q_SIZE))
    {
        if (g_q[i].used && (g_q[i].slave == slave) && (g_q[i].reg_addr == reg))
        {
            g_q[i].value = value;
            return 1;
        }
    }

    if (Queue_IsFull()) return 0;

    idx = g_q_wr;
    g_q[idx].used = 1U;
    g_q[idx].slave = slave;
    g_q[idx].reg_addr = reg;
    g_q[idx].value = value;

    g_q_wr = (uint8_t)((g_q_wr + 1U) % MOTOR_REQ_Q_SIZE);
    return 1;
}

static int Queue_Pop(MotorReq_t* out)
{
    if (Queue_IsEmpty()) return 0;

    *out = g_q[g_q_rd];
    g_q[g_q_rd].used = 0U;
    g_q_rd = (uint8_t)((g_q_rd + 1U) % MOTOR_REQ_Q_SIZE);
    return 1;
}

void Motor485_Init(void)
{
    memset(&g_motor, 0, sizeof(g_motor));
    memset(g_q, 0, sizeof(g_q));
    g_q_wr = 0;
    g_q_rd = 0;
}

void Motor485_RequestSet(uint8_t slave, MotorCmd_t cmd) { Queue_Push(slave, 0x0000, (uint16_t)cmd); }

void Motor485_RequestStopAll(void)
{
    Motor485_RequestSet(1, MOTOR_CMD_STOP);
    Motor485_RequestSet(2, MOTOR_CMD_STOP);
    Motor485_RequestSet(3, MOTOR_CMD_STOP);
}

void Motor485_OnRxFrame(const uint8_t* data, uint16_t len)
{
    if ((data == 0) || (len == 0) || (len > sizeof(g_motor.rx_buf))) return;

    memcpy(g_motor.rx_buf, data, len);
    g_motor.rx_len = len;
    g_motor.rx_flag = 1U;
}

static int CheckWriteRsp(uint8_t slave, uint16_t reg, uint16_t value)
{
    const uint8_t* p = g_motor.rx_buf;
    uint16_t len = g_motor.rx_len;
    uint16_t rsp_reg;
    uint16_t rsp_val;

    if (len != 8) return 0;
    if (!Modbus_CheckCrc(p, len)) return 0;
    if (p[0] != slave) return 0;
    if (p[1] != 0x06) return 0;

    rsp_reg = ((uint16_t)p[2] << 8) | p[3];
    rsp_val = ((uint16_t)p[4] << 8) | p[5];

    return (rsp_reg == reg) && (rsp_val == value);
}

static int CheckReadRsp(uint8_t slave, uint16_t expect_value)
{
    const uint8_t* p = g_motor.rx_buf;
    uint16_t len = g_motor.rx_len;
    uint16_t val;

    if (len != 7) return 0;
    if (!Modbus_CheckCrc(p, len)) return 0;
    if (p[0] != slave) return 0;
    if (p[1] != 0x03) return 0;
    if (p[2] != 0x02) return 0;

    val = ((uint16_t)p[3] << 8) | p[4];
    return (val == expect_value);
}

void Motor485_Task(uint32_t now)
{
    uint8_t tx[8];
    uint16_t tx_len;

    switch (g_motor.sm)
    {
        case MOTOR_SM_IDLE:
            if (Queue_Pop(&g_motor.cur_req))
            {
                g_motor.waiting_slave = g_motor.cur_req.slave;
                g_motor.waiting_value = g_motor.cur_req.value;
                g_motor.retry = 0;
                g_motor.sm = MOTOR_SM_SEND_WRITE;
            }
            break;

        case MOTOR_SM_SEND_WRITE:
            tx_len =
                Modbus_BuildWriteSingleReg(g_motor.cur_req.slave, g_motor.cur_req.reg_addr, g_motor.cur_req.value, tx);
            if (Motor485_PortSend(tx, tx_len))
            {
                g_motor.deadline = now + MOTOR_RSP_TIMEOUT_MS;
                g_motor.rx_flag = 0U;
                g_motor.sm = MOTOR_SM_WAIT_WRITE_RSP;
            }
            else
            {
                g_motor.sm = MOTOR_SM_IDLE;
            }
            break;

        case MOTOR_SM_WAIT_WRITE_RSP:
            if (g_motor.rx_flag)
            {
                g_motor.rx_flag = 0U;
                if (CheckWriteRsp(g_motor.cur_req.slave, g_motor.cur_req.reg_addr, g_motor.cur_req.value))
                {
                    g_motor.sm = MOTOR_SM_SEND_READ;
                }
                else
                {
                    if (g_motor.retry < MOTOR_RETRY_MAX)
                    {
                        g_motor.retry++;
                        g_motor.sm = MOTOR_SM_SEND_WRITE;
                    }
                    else
                    {
                        g_motor.sm = MOTOR_SM_IDLE;
                    }
                }
            }
            else if ((int32_t)(now - g_motor.deadline) >= 0)
            {
                if (g_motor.retry < MOTOR_RETRY_MAX)
                {
                    g_motor.retry++;
                    g_motor.sm = MOTOR_SM_SEND_WRITE;
                }
                else
                {
                    g_motor.sm = MOTOR_SM_IDLE;
                }
            }
            break;

        case MOTOR_SM_SEND_READ:
            tx_len = Modbus_BuildReadHoldingReg(g_motor.cur_req.slave, 0x0000, 1, tx);
            if (Motor485_PortSend(tx, tx_len))
            {
                g_motor.deadline = now + MOTOR_RSP_TIMEOUT_MS;
                g_motor.rx_flag = 0U;
                g_motor.sm = MOTOR_SM_WAIT_READ_RSP;
            }
            else
            {
                g_motor.sm = MOTOR_SM_IDLE;
            }
            break;

        case MOTOR_SM_WAIT_READ_RSP:
            if (g_motor.rx_flag)
            {
                g_motor.rx_flag = 0U;
                if (CheckReadRsp(g_motor.cur_req.slave, g_motor.cur_req.value)) { g_motor.sm = MOTOR_SM_IDLE; }
                else
                {
                    if (g_motor.retry < MOTOR_RETRY_MAX)
                    {
                        g_motor.retry++;
                        g_motor.sm = MOTOR_SM_SEND_WRITE;
                    }
                    else
                    {
                        g_motor.sm = MOTOR_SM_IDLE;
                    }
                }
            }
            else if ((int32_t)(now - g_motor.deadline) >= 0)
            {
                if (g_motor.retry < MOTOR_RETRY_MAX)
                {
                    g_motor.retry++;
                    g_motor.sm = MOTOR_SM_SEND_WRITE;
                }
                else
                {
                    g_motor.sm = MOTOR_SM_IDLE;
                }
            }
            break;

        default: g_motor.sm = MOTOR_SM_IDLE; break;
    }
}
