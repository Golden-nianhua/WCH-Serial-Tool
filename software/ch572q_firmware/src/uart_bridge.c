#include "CH57x_common.h"

#include "uart_bridge.h"

#define UART_RX_BUFFER_SIZE 512U
#define UART_RX_BUFFER_MASK (UART_RX_BUFFER_SIZE - 1U)
#define UART_TX_BUFFER_SIZE 512U
#define UART_TX_BUFFER_MASK (UART_TX_BUFFER_SIZE - 1U)
#define UART_RTS_PIN bUDM
#define UART_DTR_PIN bUDP

static uint8_t rx_buffer[UART_RX_BUFFER_SIZE];
static uint8_t tx_buffer[UART_TX_BUFFER_SIZE];
static volatile uint16_t rx_write;
static volatile uint16_t rx_read;
static volatile uint16_t tx_write;
static volatile uint16_t tx_read;
static volatile uint32_t rx_event_count;
static uint32_t current_baud = 115200U;

void UartBridge_Init(void)
{
    GPIOA_SetBits(bTXD_0);
    GPIOA_ModeCfg(bTXD_0, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(bRXD_0, GPIO_ModeIN_PU);
    UART_Remap(ENABLE, UART_TX_REMAP_PA3, UART_RX_REMAP_PA2);
    UART_DefInit();
    UART_ByteTrigCfg(UART_1BYTE_TRIG);
    UART_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART_IRQn);
    current_baud = 115200U;
}

void UartBridge_ModemInit(void)
{
    GPIOA_SetBits(UART_RTS_PIN | UART_DTR_PIN);
    GPIOA_ModeCfg(UART_RTS_PIN | UART_DTR_PIN, GPIO_ModeOut_PP_5mA);
}

void UartBridge_SetModem(uint8_t rts, uint8_t dtr)
{
    if(rts)
        GPIOA_ResetBits(UART_RTS_PIN);
    else
        GPIOA_SetBits(UART_RTS_PIN);

    if(dtr)
        GPIOA_ResetBits(UART_DTR_PIN);
    else
        GPIOA_SetBits(UART_DTR_PIN);
}

void UartBridge_Task(void)
{
    while(tx_read != tx_write && R8_UART_TFC < UART_FIFO_SIZE)
    {
        R8_UART_THR = tx_buffer[tx_read];
        tx_read = (tx_read + 1U) & UART_TX_BUFFER_MASK;
    }
}

void UartBridge_SetBaud(uint32_t baud)
{
    uint32_t divisor = (FREQ_SYS + baud * 4U) / (baud * 8U);

    if(divisor == 0U)
        divisor = 1U;
    R16_UART_DL = (uint16_t)divisor;
    current_baud = FREQ_SYS / (8U * divisor);
    UART_ByteTrigCfg(baud >= 1000000U ? UART_4BYTE_TRIG : UART_1BYTE_TRIG);
}

uint8_t UartBridge_Configure(uint32_t baud, uint8_t data_bits,
                             uint8_t stop_bits, uint8_t parity)
{
    uint8_t line_control;

    if(baud == 0U || data_bits < 5U || data_bits > 8U ||
       stop_bits < 1U || stop_bits > 2U || parity > 4U)
        return 1;

    line_control = data_bits - 5U;
    if(stop_bits == 2U)
        line_control |= RB_LCR_STOP_BIT;
    if(parity != 0U)
    {
        line_control |= RB_LCR_PAR_EN;
        line_control |= (parity - 1U) << 4;
    }

    UartBridge_SetBaud(baud);
    R8_UART_LCR = line_control;
    return 0;
}

void UartBridge_SetLineCoding(uint32_t baud, uint8_t stop_bits,
                              uint8_t parity, uint8_t data_bits)
{
    (void)UartBridge_Configure(baud, data_bits,
                               stop_bits == 2U ? 2U : 1U,
                               parity);
}

void UartBridge_SetBreak(uint8_t enabled)
{
    if(enabled)
        R8_UART_LCR |= RB_LCR_BREAK_EN;
    else
        R8_UART_LCR &= ~RB_LCR_BREAK_EN;
}

uint32_t UartBridge_CalculateBaud(uint32_t baud)
{
    uint32_t divisor = (FREQ_SYS + baud * 4U) / (baud * 8U);

    if(divisor == 0U)
        divisor = 1U;
    return FREQ_SYS / (8U * divisor);
}

uint32_t UartBridge_GetBaud(void)
{
    return current_baud;
}

uint16_t UartBridge_Available(void)
{
    return (rx_write - rx_read) & UART_RX_BUFFER_MASK;
}

uint8_t UartBridge_ReadByte(uint8_t *data)
{
    if(rx_read == rx_write)
        return 0;

    *data = rx_buffer[rx_read];
    rx_read = (rx_read + 1U) & UART_RX_BUFFER_MASK;
    return 1;
}

uint8_t UartBridge_WriteByte(uint8_t data)
{
    if(R8_UART_TFC == UART_FIFO_SIZE)
        return 0;
    R8_UART_THR = data;
    return 1;
}

uint32_t UartBridge_GetRxEventCount(void)
{
    return rx_event_count;
}

uint16_t UartBridge_Write(const uint8_t *data, uint16_t length)
{
    uint16_t written = 0;

    if(UartBridge_TxFree() < length)
        return 0;

    while(written < length)
    {
        uint16_t next = (tx_write + 1U) & UART_TX_BUFFER_MASK;
        tx_buffer[tx_write] = data[written++];
        tx_write = next;
    }
    return written;
}

uint16_t UartBridge_TxFree(void)
{
    return (tx_read - tx_write - 1U) & UART_TX_BUFFER_MASK;
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
}
