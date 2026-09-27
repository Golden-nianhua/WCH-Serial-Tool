#ifndef CH572_RF_TRANSPORT_H
#define CH572_RF_TRANSPORT_H

#include <stdint.h>
#include "rf_uart_rx.h"

#define RF_TRANSPORT_REMOTE_PAYLOAD_SIZE 36

typedef enum
{
    RF_TRANSPORT_OFF = 0,
    RF_TRANSPORT_USB_RECEIVER,
    RF_TRANSPORT_UART_NODE
} rf_transport_role_t;

void RfTransport_Init(rf_transport_role_t role, uint8_t usb_active);
void RfTransport_Task(void);
rf_transport_role_t RfTransport_GetRole(void);
void RfTransport_GetPairStatus(uint8_t *paired, uint8_t *state,
                               uint16_t *server_data);
uint8_t RfTransport_StartPairing(void);
uint8_t RfTransport_ClearPairing(void);
uint8_t RfTransport_StartScan(void);
uint8_t RfTransport_IsScanning(void);
uint8_t RfTransport_GetScanResults(rf_scan_device_t *devices,
                                   uint8_t capacity);
uint8_t RfTransport_PairDevice(const uint8_t device_id[6]);
uint8_t RfTransport_GetTxPower(void);
void RfTransport_SetTxPower(uint8_t value);
uint32_t RfTransport_GetUartBaud(void);
uint8_t RfTransport_RemoteCommand(uint8_t command, const uint8_t *request,
                                  uint8_t request_length, uint8_t *status,
                                  uint8_t *response,
                                  uint8_t *response_length);

#endif
