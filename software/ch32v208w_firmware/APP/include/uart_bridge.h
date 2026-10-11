#ifndef CH32V208_UART_BRIDGE_H
#define CH32V208_UART_BRIDGE_H

#include <stdint.h>

void UartBridge_Init(void);
void UartBridge_ModemInit(void);
void UartBridge_Task(void);
void UartBridge_SetBaud(uint32_t baud);
void UartBridge_SetLineCoding(uint32_t baud, uint8_t stop_bits,
                              uint8_t parity, uint8_t data_bits);
void UartBridge_SetBreak(uint8_t enabled);
uint8_t UartBridge_Configure(uint32_t baud, uint8_t data_bits,
                             uint8_t stop_bits, uint8_t parity);
void UartBridge_SetModem(uint8_t rts, uint8_t dtr);
uint32_t UartBridge_CalculateBaud(uint32_t baud);
uint32_t UartBridge_GetBaud(void);
uint16_t UartBridge_Available(void);
uint8_t UartBridge_ReadByte(uint8_t *data);
uint8_t UartBridge_WriteByte(uint8_t data);
uint16_t UartBridge_Write(const uint8_t *data, uint16_t length);
uint16_t UartBridge_TxFree(void);
uint32_t UartBridge_GetRxEventCount(void);

#endif
