#ifndef CH32V208_BLE_UART_SERVICE_H
#define CH32V208_BLE_UART_SERVICE_H

#include "CONFIG.h"

typedef enum
{
    BLE_UART_EVT_TX_NOTI_DISABLED = 1,
    BLE_UART_EVT_TX_NOTI_ENABLED,
    BLE_UART_EVT_DATA_RECEIVED,
    BLE_UART_EVT_CONFIG_RECEIVED,
    BLE_UART_EVT_CONFIG_NOTI_ENABLED
} ble_uart_evt_type_t;

typedef struct
{
    ble_uart_evt_type_t type;
    const uint8_t *data;
    uint16_t length;
} ble_uart_evt_t;

typedef uint8_t (*ble_uart_callback_t)(uint16_t conn_handle,
                                       const ble_uart_evt_t *event);

bStatus_t BleUartService_Add(ble_uart_callback_t callback);
uint8_t BleUartService_NotifyReady(uint16_t conn_handle);
bStatus_t BleUartService_Notify(uint16_t conn_handle,
                                attHandleValueNoti_t *notification);
uint8_t BleUartService_ConfigNotifyReady(uint16_t conn_handle);
bStatus_t BleUartService_NotifyStatus(uint16_t conn_handle,
                                      uint8_t flags,
                                      uint8_t modem_status);

#endif
