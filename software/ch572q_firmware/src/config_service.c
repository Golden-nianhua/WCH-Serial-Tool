#include "CH57x_common.h"

#include "ble_uart_app.h"
#include "config_protocol.h"
#include "config_service.h"
#include "device_config.h"
#include "uart_bridge.h"
#include "usb_device.h"

#include <string.h>

typedef struct __attribute__((packed))
{
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t fw_patch;
    uint8_t hardware;
    uint8_t cdc_ports;
    uint8_t wireless_ready;
    uint8_t protocol;
    uint8_t firmware_type;
    uint32_t system_clock;
    uint32_t app_start;
    uint32_t app_end;
    uint8_t chip_id;
    uint8_t boot_enabled;
    uint8_t mac[6];
    uint8_t unique_id[8];
} app_info_t;

/* Kept binary-compatible with the existing configuration CLI. */
typedef struct __attribute__((packed))
{
    uint8_t usb_configured;
    uint8_t wireless_ready;
    uint8_t wireless_mode;
    uint8_t paired;
    uint8_t pair_state;
    uint8_t reserved;
    uint16_t server_data;
    uint32_t uart_baud;
    uint32_t wireless_uart_baud;
} app_status_t;

typedef struct __attribute__((packed))
{
    uint8_t paired;
    uint8_t pair_state;
    uint16_t server_data;
} pair_status_t;

typedef struct __attribute__((packed))
{
    uint8_t tx_power;
    uint8_t reserved[3];
} radio_config_t;

typedef struct __attribute__((packed))
{
    uint8_t capacitance;
    uint8_t saved_capacitance;
    uint16_t samples;
    uint32_t average_ticks;
    int32_t error_ppm;
} xtal_status_t;

typedef struct __attribute__((packed))
{
    uint8_t capacitance;
    uint8_t save;
    uint8_t reserved[2];
} xtal_set_t;

static uint8_t reset_pending;
static uint8_t isp_pending;
static uint32_t reset_time;

static __HIGH_CODE __attribute__((noinline)) void erase_boot_stub_and_reset(void)
{
    while(FLASH_ROM_ERASE(0, FLASH_BLOCK_SIZE));

    FLASH_ROM_SW_RESET();
    R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1;
    R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2;
    SAFEOPERATE;
    R16_INT_LSI_TUNE = 0xffff;
    R8_RST_WDOG_CTRL |= RB_SOFTWARE_RESET;
    R8_SAFE_ACCESS_SIG = 0;
    while(1);
}

static void enter_isp_boot(void)
{
    UsbDevice_Disable();
    DelayMs(20);
    erase_boot_stub_and_reset();
}

void ConfigService_Init(void)
{
    reset_pending = 0;
    isp_pending = 0;
}

static void make_info(config_packet_t *packet)
{
    app_info_t info = {
        0, 2, 1, 0x72, 1, 1, CFG_PROTOCOL_VERSION,
        CFG_FIRMWARE_USB_BLE_UART,
        GetSysClock(), IAP_APP_START, IAP_APP_END,
        R8_CHIP_ID, (R8_GLOB_CFG_INFO & RB_CFG_BOOT_EN) != 0,
        {0}, {0}
    };
    __attribute__((aligned(4))) uint8_t id[8];
    uint16_t id_sum;

    GetMACAddress(id);
    memcpy(info.mac, id, sizeof(info.mac));
    id_sum = (uint16_t)(id[0] | id[1] << 8) +
             (uint16_t)(id[2] | id[3] << 8) +
             (uint16_t)(id[4] | id[5] << 8);
    id[6] = (uint8_t)id_sum;
    id[7] = (uint8_t)(id_sum >> 8);
    memcpy(info.unique_id, id, sizeof(info.unique_id));
    Config_MakeResponse(packet, packet, CFG_STATUS_OK,
                        &info, sizeof(info));
}

static void make_status(config_packet_t *packet)
{
    app_status_t status;

    memset(&status, 0, sizeof(status));
    status.usb_configured = UsbDevice_IsConfigured();
    status.wireless_ready = 1;
    status.wireless_mode = UsbDevice_IsConfigured() ? 0 : 3;
    status.paired = BleUart_IsConnected();
    status.pair_state = BleUart_GetState();
    status.uart_baud = UartBridge_GetBaud();
    status.wireless_uart_baud = status.uart_baud;
    Config_MakeResponse(packet, packet, CFG_STATUS_OK,
                        &status, sizeof(status));
}

static void make_xtal_status(config_packet_t *packet)
{
    xtal_status_t status;
    uint16_t samples;
    uint32_t average_ticks;
    int32_t error_ppm;

    status.capacitance =
        (uint8_t)(((R8_XT32M_TUNE & RB_XT32M_C_LOAD) >> 4) * 2U + 6U);
    status.saved_capacitance = DeviceConfig_GetXtalCap();
    UsbDevice_GetSofMeasurement(&samples, &average_ticks, &error_ppm);
    status.samples = samples;
    status.average_ticks = average_ticks;
    status.error_ppm = error_ppm;
    Config_MakeResponse(packet, packet, CFG_STATUS_OK,
                        &status, sizeof(status));
}

void ConfigService_ProcessPacket(const uint8_t *request, uint8_t *response)
{
    config_packet_t packet;

    memcpy(&packet, request, sizeof(packet));

    if(!Config_PacketValid(&packet))
    {
        memset(&packet, 0, sizeof(packet));
        packet.command = 0x7f;
        Config_MakeResponse(&packet, &packet,
                            CFG_STATUS_BAD_PACKET, NULL, 0);
    }
    else if(packet.command == CFG_CMD_GET_INFO)
    {
        make_info(&packet);
    }
    else if(packet.command == CFG_CMD_GET_STATUS)
    {
        make_status(&packet);
    }
    else if(packet.command == CFG_CMD_GET_PAIR)
    {
        const pair_status_t pair = {
            BleUart_IsConnected(), BleUart_GetState(), 0
        };
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK,
                            &pair, sizeof(pair));
    }
    else if(packet.command == CFG_CMD_GET_RADIO_CONFIG)
    {
        const radio_config_t config = {
            DeviceConfig_GetTxPower(), {0, 0, 0}
        };
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK,
                            &config, sizeof(config));
    }
    else if(packet.command == CFG_CMD_SET_RADIO_CONFIG)
    {
        if(packet.length != sizeof(radio_config_t))
        {
            Config_MakeResponse(&packet, &packet,
                                CFG_STATUS_BAD_LENGTH, NULL, 0);
        }
        else
        {
            const radio_config_t *config =
                (const radio_config_t *)packet.payload;
            uint8_t previous = DeviceConfig_GetTxPower();
            uint8_t status = CFG_STATUS_OK;

            if(!DeviceConfig_PowerSupported(config->tx_power))
                status = CFG_STATUS_STATE;
            else if(!UsbDevice_IsConfigured() &&
                    BleUart_SetTxPower(config->tx_power) != 0)
                status = CFG_STATUS_STATE;
            else if(DeviceConfig_SetTxPower(config->tx_power))
            {
                if(!UsbDevice_IsConfigured())
                    BleUart_SetTxPower(previous);
                status = CFG_STATUS_FLASH;
            }
            Config_MakeResponse(&packet, &packet, status, NULL, 0);
        }
    }
    else if(packet.command == CFG_CMD_GET_RSSI)
    {
        int8_t rssi = BleUart_GetRssi();

        Config_MakeResponse(&packet, &packet,
                            BleUart_IsConnected() ? CFG_STATUS_OK
                                                  : CFG_STATUS_STATE,
                            &rssi,
                            BleUart_IsConnected() ? sizeof(rssi) : 0);
    }
    else if(packet.command == CFG_CMD_GET_XTAL)
    {
        make_xtal_status(&packet);
    }
    else if(packet.command == CFG_CMD_SET_XTAL)
    {
        if(packet.length != sizeof(xtal_set_t))
        {
            Config_MakeResponse(&packet, &packet,
                                CFG_STATUS_BAD_LENGTH, NULL, 0);
        }
        else
        {
            const xtal_set_t *setting = (const xtal_set_t *)packet.payload;
            uint8_t status = CFG_STATUS_OK;

            if(!DeviceConfig_XtalCapSupported(setting->capacitance))
                status = CFG_STATUS_STATE;
            else if(setting->save &&
                    DeviceConfig_SetXtalCap(setting->capacitance))
                status = CFG_STATUS_FLASH;
            else
            {
                HSECFG_Capacitance((HSECapTypeDef)
                                   ((setting->capacitance - 6U) / 2U));
                UsbDevice_ResetSofMeasurement();
            }
            Config_MakeResponse(&packet, &packet, status, NULL, 0);
        }
    }
    else if(packet.command == CFG_CMD_RESET)
    {
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK, NULL, 0);
        reset_pending = 1;
        reset_time = SYS_GetSysTickCnt() +
                     GetSysClock() / (UsbDevice_IsConfigured() ? 10U : 1U);
    }
    else if(packet.command == CFG_CMD_ENTER_ISP)
    {
        if(!UsbDevice_IsConfigured())
            Config_MakeResponse(&packet, &packet,
                                CFG_STATUS_STATE, NULL, 0);
        else
        {
            Config_MakeResponse(&packet, &packet, CFG_STATUS_OK, NULL, 0);
            isp_pending = 1;
            reset_time = SYS_GetSysTickCnt() + GetSysClock() / 10U;
        }
    }
    else
    {
        Config_MakeResponse(&packet, &packet,
                            CFG_STATUS_BAD_COMMAND, NULL, 0);
    }

    memcpy(response, &packet, sizeof(packet));
}

void ConfigService_Task(void)
{
    config_packet_t request;
    config_packet_t response;

    if((reset_pending || isp_pending) &&
       (int32_t)(SYS_GetSysTickCnt() - reset_time) >= 0)
    {
        if(isp_pending)
            enter_isp_boot();
        SYS_ResetExecute();
    }

    if(UsbDevice_VendorRead((uint8_t *)&request, sizeof(request)) !=
       sizeof(request))
        return;

    ConfigService_ProcessPacket((const uint8_t *)&request,
                                (uint8_t *)&response);
    UsbDevice_VendorWrite((const uint8_t *)&response, sizeof(response));
}
