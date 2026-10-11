#include "CONFIG.h"
#include "uart_bridge.h"

#define UART_RX_BUFFER_SIZE 512U
#define UART_RX_BUFFER_MASK (UART_RX_BUFFER_SIZE - 1U)
#define UART_TX_BUFFER_SIZE 512U
#define UART_TX_BUFFER_MASK (UART_TX_BUFFER_SIZE - 1U)

static uint8_t rx_buffer[UART_RX_BUFFER_SIZE];
static uint8_t tx_buffer[UART_TX_BUFFER_SIZE];
static volatile uint16_t rx_write;
static volatile uint16_t rx_read;
static volatile uint16_t tx_write;
static volatile uint16_t tx_read;
static volatile uint32_t rx_event_count;
static uint32_t current_baud = 115200U;

static void configure_uart(uint32_t baud, uint8_t stop_bits, uint8_t parity)
{
    USART_InitTypeDef uart = {0};

    USART_Cmd(USART1, DISABLE);
    uart.USART_BaudRate = baud;
    uart.USART_WordLength = parity ? USART_WordLength_9b
                                   : USART_WordLength_8b;
    uart.USART_StopBits = stop_bits == 2U ? USART_StopBits_2
                                          : USART_StopBits_1;
    uart.USART_Parity = parity == 1U ? USART_Parity_Odd :
                        parity == 2U ? USART_Parity_Even :
                                      USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &uart);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART1, ENABLE);
    current_baud = baud;
}

void UartBridge_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    NVIC_InitTypeDef nvic = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_USART1, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    configure_uart(115200U, 1U, 0U);
    nvic.NVIC_IRQChannel = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority = 1;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
}

void UartBridge_ModemInit(void)
{
}

void UartBridge_SetModem(uint8_t rts, uint8_t dtr)
{
    (void)rts;
    (void)dtr;
}

void UartBridge_Task(void)
{
}

void UartBridge_SetBaud(uint32_t baud)
{
    configure_uart(baud, 1U, 0U);
}

uint8_t UartBridge_Configure(uint32_t baud, uint8_t data_bits,
                             uint8_t stop_bits, uint8_t parity)
{
    if(baud == 0U || baud > 12000000U || data_bits != 8U ||
       stop_bits < 1U || stop_bits > 2U || parity > 2U)
        return 1;

    configure_uart(baud, stop_bits, parity);
    return 0;
}

void UartBridge_SetLineCoding(uint32_t baud, uint8_t stop_bits,
                              uint8_t parity, uint8_t data_bits)
{
    (void)UartBridge_Configure(baud, data_bits,
                              stop_bits == 2U ? 2U : 1U, parity);
}

void UartBridge_SetBreak(uint8_t enabled)
{
    (void)enabled;
}

uint32_t UartBridge_CalculateBaud(uint32_t baud)
{
    return baud;
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
    uint16_t next = (tx_write + 1U) & UART_TX_BUFFER_MASK;

    if(next == tx_read)
        return 0;
    tx_buffer[tx_write] = data;
    tx_write = next;
    USART_ITConfig(USART1, USART_IT_TXE, ENABLE);
    return 1;
}

uint16_t UartBridge_Write(const uint8_t *data, uint16_t length)
{
    uint16_t written = 0;

    if(UartBridge_TxFree() < length)
        return 0;
    while(written < length)
        UartBridge_WriteByte(data[written++]);
    return written;
}

uint16_t UartBridge_TxFree(void)
{
    return (tx_read - tx_write - 1U) & UART_TX_BUFFER_MASK;
}

uint32_t UartBridge_GetRxEventCount(void)
{
    return rx_event_count;
}

__attribute__((interrupt("WCH-Interrupt-fast")))
void USART1_IRQHandler(void)
{
    if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        uint16_t next = (rx_write + 1U) & UART_RX_BUFFER_MASK;
        uint8_t data = (uint8_t)USART_ReceiveData(USART1);

        if(next != rx_read)
        {
            rx_buffer[rx_write] = data;
            rx_write = next;
            rx_event_count++;
        }
    }

    if(USART_GetITStatus(USART1, USART_IT_TXE) != RESET)
    {
        if(tx_read == tx_write)
            USART_ITConfig(USART1, USART_IT_TXE, DISABLE);
        else
        {
            USART_SendData(USART1, tx_buffer[tx_read]);
            tx_read = (tx_read + 1U) & UART_TX_BUFFER_MASK;
        }
    }
}
