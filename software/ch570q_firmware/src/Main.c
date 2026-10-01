#include "CH57x_common.h"

#include "config_service.h"
#include "rf_transport.h"
#include "uart_bridge.h"
#include "usb_device.h"

#define LED_PIN (1U << 7)

typedef enum
{
    APP_USB_WAIT = 0,
    APP_USB_RF_RECEIVER,
    APP_RF_UART_NODE
} app_mode_t;

static app_mode_t app_mode;
static uint32_t usb_wait_start;

static uint32_t millis(void)
{
    return SYS_GetSysTickCnt() / (GetSysClock() / 1000U);
}

static void timebase_init(void)
{
    SysTick->CTLR = 0;
    SysTick->CNT = 0;
    SysTick->CMP = UINT32_MAX;
    SysTick->CTLR = SysTick_CTLR_STCLK | SysTick_CTLR_STE;
}

static void led_init(void)
{
    GPIOA_ResetBits(LED_PIN);
    GPIOA_ModeCfg(LED_PIN, GPIO_ModeOut_PP_5mA);
}

static void led_write(uint8_t on)
{
    if(on)
        GPIOA_SetBits(LED_PIN);
    else
        GPIOA_ResetBits(LED_PIN);
}

static void led_task(void)
{
    static uint32_t previous_activity;
    static uint32_t activity_time;
    uint32_t now = millis();
    uint32_t activity = UsbDevice_GetActivityCount();
    uint8_t paired;
    uint8_t pair_state;
    uint16_t server_data;

    if(app_mode == APP_RF_UART_NODE)
    {
        RfTransport_GetPairStatus(&paired, &pair_state, &server_data);
        (void)pair_state;
        (void)server_data;
        if(!paired)
            led_write(((now / 250U) & 1U) == 0U);
        else if(pair_state == BOUND_STATUS_EST)
            led_write((now % 1000U) < 60U);
        else
            led_write((now % 1000U) < 60U ||
                      ((now % 1000U) >= 160U && (now % 1000U) < 220U));
        return;
    }

    if(activity != previous_activity)
    {
        previous_activity = activity;
        activity_time = now;
    }

    if((uint32_t)(now - activity_time) < 40U)
        led_write(1);
    else if(app_mode == APP_USB_WAIT)
        led_write(((now / 100U) & 1U) == 0U);
    else
    {
        RfTransport_GetPairStatus(&paired, &pair_state, &server_data);
        (void)pair_state;
        (void)server_data;
        if(paired)
            led_write((now % 1000U) < 60U);
        else
            led_write(((now / 250U) & 1U) == 0U);
    }
}

int main(void)
{
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);

    timebase_init();
    led_init();
    UartBridge_Init();
    UsbDevice_Init();
    ConfigService_Init();
    app_mode = APP_USB_WAIT;
    usb_wait_start = millis();

    while(1)
    {
        if(app_mode != APP_RF_UART_NODE)
            UsbDevice_Task();

        if(app_mode == APP_USB_WAIT)
        {
            if(UsbDevice_IsConfigured())
            {
                RfTransport_Init(RF_TRANSPORT_USB_RECEIVER, 1);
                app_mode = APP_USB_RF_RECEIVER;
            }
            else if((uint32_t)(millis() - usb_wait_start) >= 500U)
            {
                UsbDevice_Disable();
                RfTransport_Init(RF_TRANSPORT_UART_NODE, 0);
                app_mode = APP_RF_UART_NODE;
            }
        }

        if(app_mode == APP_USB_RF_RECEIVER)
        {
            RfTransport_Task();
            ConfigService_Task();
        }
        else if(app_mode == APP_RF_UART_NODE)
        {
            RfTransport_Task();
        }

        led_task();
    }
}
