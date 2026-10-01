/********************************** (C) COPYRIGHT *******************************
* File Name          : rf_uart_tx.h
* Author             : WCH
* Version            : V1.0
* Date               : 2022/03/10
* Description        : 
*******************************************************************************/

#ifndef __RF_UART_TX_H
#define __RF_UART_TX_H

#ifdef __cplusplus
extern "C"
{
#endif
#include <CH572rf.h>
#include "CH57x_common.h"
#include "buf.h"


#define  ADV_INTERVAL        20

#define  RESEND_COUNT        40

#define  BOUND_INFO_FLASH_ADDR         (1024*236)
#define  RF_CONFIG_FLASH_ADDR          (1024*232)

extern uint32_t  hostRfRxFlag;
extern struct simple_buf *hostRfBuf;

/* rf tx status */
#define   STA_IDLE          0x00
#define   STA_BUSY          0x01
#define   STA_RESEND        0x02

void RF_UartTxInit( void );
void RF_StatusQuery( void );
void RF_GetPairStatus(uint8_t *paired, uint8_t *state, uint16_t *server_data);
uint8_t RF_ClearPairing(void);
uint8_t RF_TxGetRssi(int8_t *rssi);
uint8_t RF_LoadTxPower(uint8_t *value);
uint8_t RF_SaveTxPower(uint8_t value);
uint8_t RF_RemoteTakeRequest(uint8_t *transaction, uint8_t *command,
                             uint8_t *payload, uint8_t *length);
void RF_RemoteSetResponse(uint8_t transaction, uint8_t command,
                          uint8_t status, const uint8_t *payload,
                          uint8_t length, uint8_t reset_after_ack);
uint8_t RF_RemoteResetReady(void);


#ifdef __cplusplus
}
#endif

#endif
