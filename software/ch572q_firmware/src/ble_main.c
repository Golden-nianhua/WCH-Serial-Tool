#include "CONFIG.h"
#include "HAL.h"

#include "app_handoff.h"
#include "ble_uart_app.h"
#include "device_config.h"
#include "mode_memory.h"
#include "uart_bridge.h"

#define LED_PIN (1U << 7)

__attribute__((aligned(4)))
uint32_t mode_memory[MODE_MEMORY_SIZE / sizeof(uint32_t)];

__attribute__((section(".handoff"), aligned(4)))
volatile app_handoff_t app_handoff;

static uint32_t millis(void)
{
    return SYS_GetSysTickCnt() / (GetSysClock() / 1000U);
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
    uint32_t now = millis();

    if(BleUart_IsConnected())
        led_write((now % 1000U) < 60U);
    else
        led_write(((now / 250U) & 1U) == 0U);
}

void HardFault_Handler(void)
{
    while(1);
}

int main(void)
{
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);

    led_init();
    DeviceConfig_Init();
    HSECFG_Capacitance((HSECapTypeDef)
                       ((DeviceConfig_GetXtalCap() - 6U) / 2U));
    UartBridge_Init();
    UartBridge_ModemInit();
    CH57x_BLEInit();
    HAL_Init();
    GAPRole_PeripheralInit();
    BleUart_Init();
    BleUart_SetTxPower(DeviceConfig_GetTxPower());

    while(1)
    {
        TMOS_SystemProcess();
        UartBridge_Task();
        BleUart_Poll();
        led_task();
    }
}
