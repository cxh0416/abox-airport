#ifndef LED_MESSAGE_H
#define LED_MESSAGE_H

#include <stdint.h>

void LedMessage_Init(void);
void LedMessage_Task(void);

/* value 格式: "time:hex"
 * 例如:
 *   "0:C4E3BAC3"                    -> 一直显示“你好”
 *   "5:C4E3BAC348454C4C4F313233"    -> 显示5秒“你好HELLO123”
 *
 * 返回:
 *   0  成功
 *  <0  失败
 */
int LedMessage_ShowFromValue(const char* value, char* err, uint16_t err_len);

void LedMessage_OnBusRx(const uint8_t* data, uint16_t len);

#endif
