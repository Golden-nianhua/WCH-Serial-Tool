#include "CH57x_common.h"

#include "uart.h"
#include "uart_bridge.h"
#include "usb_device.h"
#include "usb_uart.h"

#include "rf_legacy_port.h"

LINE_CODE Uart0Para = {115200U, 0, 0, 8, 0};
LINE_CODE RfUartPara = {115200U, 0, 0, 8, 0};

static uint16_t status_period_ms;
static uint32_t next_status_tick;
static uint32_t last_rx_event;
static uint32_t rx_quiet_tick;
static uint32_t pending_baud;
static uint8_t pending_stop_bits;
static uint8_t pending_parity;
static uint8_t pending_data_bits;
static volatile uint8_t line_coding_pending;

#define TICKS_PER_MS (FREQ_SYS / 1000U)

void RfLegacyPort_Init(void)
{
    uint32_t now = SysTick->CNTL;

    status_period_ms = 10U;
    next_status_tick = now + status_period_ms * TICKS_PER_MS;
    last_rx_event = UartBridge_GetRxEventCount();
    rx_quiet_tick = now;
    line_coding_pending = 0;
}

void RfLegacyPort_Task(void)
{
    uint32_t now;

    if(!line_coding_pending)
        return;

    line_coding_pending = 0;
    UartBridge_SetLineCoding(pending_baud, pending_stop_bits,
                             pending_parity, pending_data_bits);
    now = SysTick->CNTL;
    next_status_tick = now + status_period_ms * TICKS_PER_MS;
    rx_quiet_tick = now;
    last_rx_event = UartBridge_GetRxEventCount();
}

void UART_Init(void)
{
}

void UART_InitMode(uint8_t usb_active)
{
    if(!usb_active)
    {
        GPIOA_SetBits(RTS | DTR);
        GPIOA_ModeCfg(RTS | DTR, GPIO_ModeOut_PP_5mA);
    }
    RfLegacyPort_Init();
}

__HIGH_CODE
void UART_SetTimer(uint16_t ms)
{
    status_period_ms = ms;
    next_status_tick = SysTick->CNTL + (uint32_t)ms * TICKS_PER_MS;
}

__HIGH_CODE
void UART_SetBuad(uint32_t baud)
{
    UART_SetConfig(baud, Uart0Para.StopBits,
                   Uart0Para.ParityType, Uart0Para.DataBits);
}

__HIGH_CODE
void UART_SetConfig(uint32_t baud, uint8_t stop_bits,
                    uint8_t parity, uint8_t data_bits)
{
    Uart0Para.BaudRate = baud;
    Uart0Para.StopBits = stop_bits;
    Uart0Para.ParityType = parity;
    Uart0Para.DataBits = data_bits;
    pending_baud = baud;
    pending_stop_bits = stop_bits;
    pending_parity = parity;
    pending_data_bits = data_bits;
    line_coding_pending = 1;
}

__HIGH_CODE
uint8_t UART_RxQuery(void *buf, typeBufSize *len)
{
    uint32_t now = SysTick->CNTL;
    uint32_t event = UartBridge_GetRxEventCount();
    uint16_t available = UartBridge_Available();
    uint8_t *data = buf;
    typeBufSize requested = *len > 64U ? 64U : *len;
    typeBufSize count = 0;

    if(event != last_rx_event)
    {
        last_rx_event = event;
        rx_quiet_tick = now;
    }

    if(available &&
       (available >= requested ||
        (uint32_t)(now - rx_quiet_tick) >= TICKS_PER_MS))
    {
        while(count < requested && UartBridge_ReadByte(&data[count]))
            count++;
        *len = count;
        return 0;
    }

    if((int32_t)(now - next_status_tick) >= 0)
    {
        next_status_tick = now + (uint32_t)status_period_ms * TICKS_PER_MS;
        *len = 0;
        return 0x80;
    }

    *len = 0;
    return 0xff;
}

__HIGH_CODE
void UART_Send(char *data, uint16_t size)
{
    uint16_t index = 0;

    while(index < size)
    {
        if(UartBridge_WriteByte((uint8_t)data[index]))
            index++;
    }
}

__HIGH_CODE
uint8_t USB_RfRxQuery(void *buf, typeBufSize *len)
{
    uint32_t baud;
    uint8_t stop_bits;
    uint8_t parity;
    uint8_t data_bits;
    uint8_t io_status;
    uint8_t length;

    if(UsbDevice_RfLineCoding(&baud, &stop_bits, &parity, &data_bits,
                              &io_status))
    {
        RfUartPara.BaudRate = baud;
        RfUartPara.StopBits = stop_bits;
        RfUartPara.ParityType = parity;
        RfUartPara.DataBits = data_bits;
        RfUartPara.ioStaus = io_status;
        *len = 0;
        return 0x80;
    }

    length = *len > 64U ? 64U : (uint8_t)*len;
    length = UsbDevice_RfRead(buf, length);
    *len = length;
    return length ? 0 : 0xff;
}

uint8_t USB_IsConfigured(void)
{
    return UsbDevice_IsConfigured();
}
