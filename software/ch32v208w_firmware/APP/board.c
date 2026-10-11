#include "CONFIG.h"
#include "board.h"

#define PA_TXEN_PIN             GPIO_Pin_3
#define PA_RXEN_PIN             GPIO_Pin_4
#define STATUS_LED_PIN          GPIO_Pin_13
#define TIMER_COUNTER_HZ        1000000U
#define TIMER_PERIOD_COUNTS     1000U

static blePaControlConfig_t pa_control;
static volatile uint32_t board_millis;

static void StatusLed_On(void)
{
    GPIOC->BCR = STATUS_LED_PIN;
}

static void StatusLed_Off(void)
{
    GPIOC->BSHR = STATUS_LED_PIN;
}

static void StatusLed_Write(uint8_t on)
{
    if(on)
    {
        StatusLed_On();
    }
    else
    {
        StatusLed_Off();
    }
}

void Board_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_TimeBaseInitTypeDef timer = {0};
    NVIC_InitTypeDef nvic = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    // External PA control inputs are active high.
    GPIO_ResetBits(GPIOD, PA_TXEN_PIN | PA_RXEN_PIN);
    gpio.GPIO_Pin = PA_TXEN_PIN | PA_RXEN_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOD, &gpio);

    StatusLed_Off();
    gpio.GPIO_Pin = STATUS_LED_PIN;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOC, &gpio);

    // TIM2 provides the 1 ms status LED time base.
    timer.TIM_Prescaler = (uint16_t)(SystemCoreClock / TIMER_COUNTER_HZ - 1U);
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_Period = TIMER_PERIOD_COUNTS - 1U;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &timer);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel = TIM2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 3;
    nvic.NVIC_IRQChannelSubPriority = 3;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    TIM_Cmd(TIM2, ENABLE);
}

void Board_PAControlInit(void)
{
    // BLE_PAControlInit must be called after GAPRole_PeripheralInit.
    pa_control.txEnableGPIO = (uint32_t)&GPIOD->BSHR;
    pa_control.txDisableGPIO = (uint32_t)&GPIOD->BCR;
    pa_control.tx_pin = PA_TXEN_PIN;
    pa_control.rxEnableGPIO = (uint32_t)&GPIOD->BSHR;
    pa_control.rxDisableGPIO = (uint32_t)&GPIOD->BCR;
    pa_control.rx_pin = PA_RXEN_PIN;

    GPIO_ResetBits(GPIOD, PA_TXEN_PIN | PA_RXEN_PIN);
    GPIO_SetBits(GPIOD, PA_RXEN_PIN);
    BLE_PAControlInit(&pa_control);
}

void Board_LedTask(uint8_t connected, uint32_t usb_activity)
{
    static uint32_t previous_activity;
    static uint32_t activity_time;
    uint32_t now = board_millis;

    if(usb_activity != previous_activity)
    {
        previous_activity = usb_activity;
        activity_time = now;
    }

    if((uint32_t)(now - activity_time) < 40U)
    {
        StatusLed_On();
    }
    else if(connected)
    {
        StatusLed_Write((now % 1000U) < 60U);
    }
    else
    {
        StatusLed_Write(((now / 250U) & 1U) == 0U);
    }
}

__attribute__((interrupt("WCH-Interrupt-fast")))
void TIM2_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        board_millis++;
    }
}
