#include "CH57x_common.h"

#include "rf_uart_tx.h"
#include "uart_bridge.h"

#define UART_RX_BUFFER_SIZE 1024U
#define UART_RX_BUFFER_MASK (UART_RX_BUFFER_SIZE - 1U)

static uint8_t rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t rx_write;
static volatile uint16_t rx_read;
static volatile uint32_t rx_event_count;
static uint32_t current_baud;

static uint32_t actual_baud(uint32_t baud)
{
    uint32_t divisor = (FREQ_SYS + baud * 4U) / (baud * 8U);

    if(divisor == 0U)
        divisor = 1U;
    return FREQ_SYS / (8U * divisor);
}

void UartBridge_Init(void)
{
    GPIOA_SetBits(bTXD_0);
    GPIOA_ModeCfg(bTXD_0, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(bRXD_0, GPIO_ModeIN_PU);
    UART_Remap(ENABLE, UART_TX_REMAP_PA3, UART_RX_REMAP_PA2);
    UART_DefInit();
    UART_ByteTrigCfg(UART_1BYTE_TRIG);
    UART_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT |
                        RB_IER_THR_EMPTY);
    PFIC_EnableIRQ(UART_IRQn);
    current_baud = 115200U;
}

void UartBridge_SetLineCoding(uint32_t baud, uint8_t stop_bits,
                              uint8_t parity, uint8_t data_bits)
{
    uint32_t divisor;
    uint8_t lcr = 0;

    divisor = (FREQ_SYS + baud * 4U) / (baud * 8U);
    if(divisor == 0U)
        divisor = 1U;
    R16_UART_DL = (uint16_t)divisor;
    current_baud = FREQ_SYS / (8U * divisor);
    UART_ByteTrigCfg(baud >= 1000000U ? UART_4BYTE_TRIG : UART_1BYTE_TRIG);
    if(data_bits >= 5U && data_bits <= 8U)
        lcr = data_bits - 5U;
    if(stop_bits)
        lcr |= RB_LCR_STOP_BIT;
    if(parity)
    {
        lcr |= RB_LCR_PAR_EN;
        lcr |= ((parity - 1U) & 0x03U) << 4;
    }
    R8_UART_LCR = (R8_UART_LCR & RB_LCR_BREAK_EN) | lcr;
}

void UartBridge_SetBreak(uint8_t enabled)
{
    if(enabled)
        R8_UART_LCR |= RB_LCR_BREAK_EN;
    else
        R8_UART_LCR &= ~RB_LCR_BREAK_EN;
}

uint32_t UartBridge_GetBaud(void)
{
    return current_baud;
}

uint32_t UartBridge_CalculateBaud(uint32_t baud)
{
    return actual_baud(baud);
}

__HIGH_CODE
uint16_t UartBridge_Available(void)
{
    return (rx_write - rx_read) & UART_RX_BUFFER_MASK;
}

__HIGH_CODE
uint8_t UartBridge_ReadByte(uint8_t *data)
{
    if(rx_read == rx_write)
        return 0;
    *data = rx_buffer[rx_read];
    rx_read = (rx_read + 1U) & UART_RX_BUFFER_MASK;
    return 1;
}

__HIGH_CODE
uint8_t UartBridge_WriteByte(uint8_t data)
{
    if(R8_UART_TFC == UART_FIFO_SIZE)
        return 0;
    R8_UART_THR = data;
    return 1;
}

__HIGH_CODE
uint32_t UartBridge_GetRxEventCount(void)
{
    return rx_event_count;
}

__INTERRUPT
__HIGH_CODE
void UART_IRQHandler(void)
{
    uint8_t interrupt = UART_GetITFlag();

    if(interrupt == UART_II_LINE_STAT)
    {
        (void)UART_GetLinSTA();
    }
    else if(interrupt == UART_II_RECV_RDY ||
            interrupt == UART_II_RECV_TOUT)
    {
        while(R8_UART_RFC)
        {
            uint16_t next = (rx_write + 1U) & UART_RX_BUFFER_MASK;
            uint8_t data = R8_UART_RBR;

            if(next != rx_read)
            {
                rx_buffer[rx_write] = data;
                rx_write = next;
                rx_event_count++;
            }
        }
    }
    else if(hostRfRxFlag)
    {
        uint8_t data[UART_FIFO_SIZE];
        typeBufSize length = UART_FIFO_SIZE - R8_UART_TFC;
        uint8_t index;

        hostRfRxFlag = read_buf(hostRfBuf, data, &length);
        for(index = 0; index < length; index++)
        {
            R8_UART_THR = data[index];
        }
    }
}
