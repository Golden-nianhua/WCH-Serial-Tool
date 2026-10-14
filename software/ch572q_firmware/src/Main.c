#include "CONFIG.h"

#include "app_handoff.h"
#include "config_service.h"
#include "device_config.h"
#include "mode_memory.h"
#include "uart_bridge.h"
#include "usb_device.h"

#define LED_PIN (1U << 7)
#define USB_ENUMERATION_WAIT_MS 500U

__attribute__((aligned(4)))
uint32_t mode_memory[MODE_MEMORY_SIZE / sizeof(uint32_t)];

__attribute__((section(".handoff"), aligned(4)))
volatile app_handoff_t app_handoff;

static uint32_t usb_wait_start;
static uint32_t usb_wait_activity;
static uint8_t usb_host_seen;

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

    if(activity != previous_activity)
    {
        previous_activity = activity;
        activity_time = now;
    }

    if((uint32_t)(now - activity_time) < 40U ||
       UsbDevice_IsConfigured())
        led_write(1);
    else
        led_write(((now / 100U) & 1U) == 0U);
}

static void start_ble_app(void)
{
    uint8_t mac[8] __attribute__((aligned(4)));
    uint8_t index;

    GetMACAddress(mac);
    for(index = 0; index < sizeof(app_handoff.mac); index++)
        app_handoff.mac[5U - index] = mac[index];
    app_handoff.check = ~BLE_APP_MAGIC;
    app_handoff.magic = BLE_APP_MAGIC;

    UsbDevice_Disable();
    SYS_ResetExecute();
    while(1);
}

int main(void)
{
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(CLK_SOURCE_HSE_PLL_24MHz);

    timebase_init();
    led_init();
    DeviceConfig_Init();
    HSECFG_Capacitance((HSECapTypeDef)
                       ((DeviceConfig_GetXtalCap() - 6U) / 2U));
    UartBridge_Init();
    UsbDevice_Init();
    ConfigService_Init();
    usb_wait_start = millis();
    usb_wait_activity = UsbDevice_GetActivityCount();
    usb_host_seen = 0;

    while(1)
    {
        UsbDevice_Task();

        if(!UsbDevice_IsConfigured())
        {
            uint32_t activity = UsbDevice_GetActivityCount();

            if(activity != usb_wait_activity)
            {
                usb_wait_activity = activity;
                usb_wait_start = millis();
                usb_host_seen = 1;
            }
            if(!usb_host_seen &&
               (uint32_t)(millis() - usb_wait_start) >=
               USB_ENUMERATION_WAIT_MS)
                start_ble_app();
        }

        ConfigService_Task();
        led_task();
    }
}
