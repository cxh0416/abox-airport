#ifndef __RC_PROTOCOL_H__
#define __RC_PROTOCOL_H__

#include <stdint.h>

#include "rc_types.h"

void RC_Parse181(RcData_t* rc, const uint8_t data[8]);
void RC_Parse281(RcData_t* rc, const uint8_t data[8]);

void RC_Build10A(const RcData_t* rc, uint8_t data[8]);
void RC_Build10B(const RcData_t* rc, uint8_t data[8]);
void RC_Build10B_Standby(uint8_t data[8]);

#endif
