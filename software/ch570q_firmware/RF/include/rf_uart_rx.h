/********************************** (C) COPYRIGHT *******************************
* File Name          : rf_uart_rx.h
* Author             : WCH
* Version            : V1.0
* Date               : 2022/03/10
* Description        : 
*******************************************************************************/

#ifndef __RF_UART_RX_H
#define __RF_UART_RX_H

#ifdef __cplusplus
extern "C"
{
#endif
#include <CH572rf.h>
#include "CH57x_common.h"
#include "buf.h"
#include "rf.h"

#define   RF_BUF_LEN   128


#define  CONN_INTERVAL     10
#define  CONN_TIMEOUT      100
#define  CONN_PHY_TYPE     1   // 2M

#define RF_SCAN_MAX_DEVICES 4

#define RF_REMOTE_EXCHANGE_PENDING  0
#define RF_REMOTE_EXCHANGE_COMPLETE 1
#define RF_REMOTE_EXCHANGE_BUSY     2
#define RF_REMOTE_EXCHANGE_TIMEOUT  3

typedef struct __attribute__((packed))
{
    uint8_t device_id[6];
    int8_t rssi;
    uint8_t reserved;
    uint16_t server_data;
} rf_scan_device_t;

typedef union
{
    rf_scan_device_t scan_devices[RF_SCAN_MAX_DEVICES];
    rfRemoteMessage_t remote_message;
} rf_role_control_t;

extern rf_role_control_t rf_role_control;

void RF_UartRxInit( void );
uint8_t RF_RxQuery( void *buf, typeBufSize *len );
void RF_ScanStart(void);
uint8_t RF_ScanIsActive(void);
uint8_t RF_ScanGetResults(rf_scan_device_t *devices, uint8_t capacity);
uint8_t RF_ScanSelect(const uint8_t device_id[6]);
uint8_t RF_RxGetRssi(int8_t *rssi);
uint8_t RF_RemoteExchange(uint8_t command, const uint8_t *request,
                          uint8_t request_length, uint8_t *status,
                          uint8_t *response, uint8_t *response_length);


#ifdef __cplusplus
}
#endif

#endif
