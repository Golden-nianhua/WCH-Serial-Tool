#ifndef CH572_UART_BRIDGE_H
#define CH572_UART_BRIDGE_H

#include <stdint.h>

void UartBridge_Init(void);
void UartBridge_SetLineCoding(uint32_t baud, uint8_t stop_bits,
                              uint8_t parity, uint8_t data_bits);
void UartBridge_SetBreak(uint8_t enabled);
uint32_t UartBridge_GetBaud(void);
uint32_t UartBridge_CalculateBaud(uint32_t baud);
uint16_t UartBridge_Available(void);
uint8_t UartBridge_ReadByte(uint8_t *data);
uint8_t UartBridge_WriteByte(uint8_t data);
uint32_t UartBridge_GetRxEventCount(void);

#endif
