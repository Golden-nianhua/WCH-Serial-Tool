#ifndef CH32V208_BLE_UART_APP_H
#define CH32V208_BLE_UART_APP_H

#include <stdint.h>

enum
{
    BLE_UART_STATE_STARTING = 0,
    BLE_UART_STATE_ADVERTISING = 1,
    BLE_UART_STATE_CONNECTED = 2
};

void BleUart_Init(void);
void BleUart_Poll(void);
uint8_t BleUart_IsConnected(void);
uint8_t BleUart_GetState(void);
int8_t BleUart_GetRssi(void);
uint8_t BleUart_GetTxPower(void);
uint8_t BleUart_SetTxPower(uint8_t power);
void BleUart_GetPhy(uint8_t *tx, uint8_t *rx, uint8_t *status);

#endif
