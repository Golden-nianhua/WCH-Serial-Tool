#include "CONFIG.h"
#include "HAL.h"

#include "ble_uart_app.h"
#include "board.h"
#include "config_service.h"
#include "uart_bridge.h"
#include "usb_device.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
const uint8_t MacAddr[6] = {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};
#endif

__attribute__((section(".highcode")))
__attribute__((noinline))
static void Main_Circulation(void)
{
    while(1)
    {
        TMOS_SystemProcess();
        UartBridge_Task();
        BleUart_Poll();
        UsbDevice_Task();
        ConfigService_Task();
        Board_LedTask(BleUart_IsConnected(), UsbDevice_GetActivityCount());
    }
}

int main(void)
{
    SystemCoreClockUpdate();
    Delay_Init();
    Board_Init();
    UartBridge_Init();
    WCHBLE_Init();
    HAL_Init();
    GAPRole_PeripheralInit();
    Board_PAControlInit();
    ConfigService_Init();
    BleUart_Init();
    UsbDevice_Init();
    Main_Circulation();
}
