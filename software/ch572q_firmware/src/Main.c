#include "CONFIG.h"
#include "HAL.h"

#include "ble_uart_app.h"
#include "config_service.h"
#include "device_config.h"
#include "uart_bridge.h"
#include "usb_device.h"

#define LED_PIN (1U << 7)
#define USB_ENUMERATION_WAIT_MS 500U

typedef enum
{
    APP_USB_WAIT = 0,
    APP_USB_CONFIG,
    APP_BLE_UART
} app_mode_t;

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

static app_mode_t app_mode;
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

    if((uint32_t)(now - activity_time) < 40U)
        led_write(1);
    else if(app_mode == APP_USB_WAIT)
        led_write(((now / 100U) & 1U) == 0U);
    else if(app_mode == APP_USB_CONFIG)
        led_write(1);
    else if(BleUart_IsConnected())
        led_write((now % 1000U) < 60U);
    else
        led_write(((now / 250U) & 1U) == 0U);
}

int main(void)
{
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);

    timebase_init();
    led_init();
    UsbDevice_Init();
    DeviceConfig_Init();
    ConfigService_Init();
    app_mode = APP_USB_WAIT;
    usb_wait_start = millis();
    usb_wait_activity = UsbDevice_GetActivityCount();
    usb_host_seen = 0;

    while(1)
    {
        if(app_mode != APP_BLE_UART)
            UsbDevice_Task();

        if(app_mode == APP_USB_WAIT)
        {
            uint32_t activity = UsbDevice_GetActivityCount();

            if(activity != usb_wait_activity)
            {
                usb_wait_activity = activity;
                usb_wait_start = millis();
                usb_host_seen = 1;
            }
            if(UsbDevice_IsConfigured())
            {
                app_mode = APP_USB_CONFIG;
            }
            else if(!usb_host_seen &&
                    (uint32_t)(millis() - usb_wait_start) >=
                    USB_ENUMERATION_WAIT_MS)
            {
                UsbDevice_Disable();
                UartBridge_Init();
                UartBridge_ModemInit();
                CH57x_BLEInit();
                HAL_Init();
                GAPRole_PeripheralInit();
                BleUart_Init();
                BleUart_SetTxPower(DeviceConfig_GetTxPower());
                app_mode = APP_BLE_UART;
            }
        }

        if(app_mode == APP_BLE_UART)
        {
            TMOS_SystemProcess();
            UartBridge_Task();
            BleUart_Poll();
        }
        ConfigService_Task();
        led_task();
    }
}
