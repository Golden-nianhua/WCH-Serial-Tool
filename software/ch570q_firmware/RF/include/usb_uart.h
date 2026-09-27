#ifndef CH570Q_USB_UART_H
#define CH570Q_USB_UART_H

#include "CH57x_common.h"
#include "buf.h"

typedef struct __PACKED
{
    uint32_t BaudRate;
    uint8_t StopBits;
    uint8_t ParityType;
    uint8_t DataBits;
    uint8_t ioStaus;
} LINE_CODE;
typedef LINE_CODE *PLINE_CODE;

extern LINE_CODE Uart0Para;
extern LINE_CODE RfUartPara;

void USB_Init(void);
void USB_StatusQuery(void);
uint8_t USB_RxQuery(void *buf, typeBufSize *len);
uint8_t USB_RfRxQuery(void *buf, typeBufSize *len);
uint8_t USB_IsConfigured(void);
uint8_t USB_SendData(const uint8_t *buf, uint8_t len);
uint8_t USB_ConfigQuery(uint8_t *buf, uint8_t *len);
uint8_t USB_ConfigSend(const uint8_t *buf, uint8_t len);
void App_USB_Disable(void);
void USB_SetRfMode(uint8_t enable);

#endif
