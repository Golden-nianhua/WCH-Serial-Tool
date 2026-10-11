#include "CONFIG.h"

#include "ble_uart_app.h"
#include "config_protocol.h"
#include "config_service.h"
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
    uint8_t connected;
    uint8_t tx_phy;
    uint8_t rx_phy;
    uint8_t update_status;
} phy_status_t;

static uint8_t reset_pending;
static uint32_t reset_deadline;

void ConfigService_Init(void)
{
    reset_pending = 0;
}

static void make_info(config_packet_t *packet)
{
    app_info_t info = {
        0, 1, 0, 0x20, 0, 1, CFG_PROTOCOL_VERSION,
        CFG_FIRMWARE_CH32_BLE_UART,
        0, 0x00000000U, 0x00028000U,
        0x08, 0, {0}, {0}
    };
    uint8_t mac[6];

    SystemCoreClockUpdate();
    info.system_clock = SystemCoreClock;
    FLASH_GetMACAddress(mac);
    memcpy(info.mac, mac, sizeof(info.mac));
    memcpy(info.unique_id, mac, sizeof(mac));
    info.unique_id[6] = mac[0] ^ mac[2] ^ mac[4];
    info.unique_id[7] = mac[1] ^ mac[3] ^ mac[5];
    Config_MakeResponse(packet, packet, CFG_STATUS_OK, &info, sizeof(info));
}

static void make_status(config_packet_t *packet)
{
    app_status_t status;

    memset(&status, 0, sizeof(status));
    status.usb_configured = UsbDevice_IsConfigured();
    status.wireless_ready = 1;
    status.wireless_mode = 3;
    status.paired = BleUart_IsConnected();
    status.pair_state = BleUart_GetState();
    status.uart_baud = UartBridge_GetBaud();
    status.wireless_uart_baud = status.uart_baud;
    Config_MakeResponse(packet, packet, CFG_STATUS_OK, &status, sizeof(status));
}

void ConfigService_ProcessPacket(const uint8_t *request, uint8_t *response)
{
    config_packet_t packet;

    memcpy(&packet, request, sizeof(packet));
    if(!Config_PacketValid(&packet))
    {
        memset(&packet, 0, sizeof(packet));
        packet.command = 0x7f;
        Config_MakeResponse(&packet, &packet, CFG_STATUS_BAD_PACKET, NULL, 0);
    }
    else if(packet.command == CFG_CMD_GET_INFO)
        make_info(&packet);
    else if(packet.command == CFG_CMD_GET_STATUS)
        make_status(&packet);
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
        const radio_config_t config = {BleUart_GetTxPower(), {0, 0, 0}};
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK,
                            &config, sizeof(config));
    }
    else if(packet.command == CFG_CMD_SET_RADIO_CONFIG)
    {
        if(packet.length != sizeof(radio_config_t))
            Config_MakeResponse(&packet, &packet, CFG_STATUS_BAD_LENGTH, NULL, 0);
        else
        {
            const radio_config_t *config = (const radio_config_t *)packet.payload;
            uint8_t result = BleUart_SetTxPower(config->tx_power);
            Config_MakeResponse(&packet, &packet,
                                result ? CFG_STATUS_STATE : CFG_STATUS_OK,
                                NULL, 0);
        }
    }
    else if(packet.command == CFG_CMD_GET_RSSI)
    {
        int8_t rssi = BleUart_GetRssi();
        Config_MakeResponse(&packet, &packet,
                            BleUart_IsConnected() ? CFG_STATUS_OK
                                                  : CFG_STATUS_STATE,
                            &rssi, BleUart_IsConnected() ? sizeof(rssi) : 0);
    }
    else if(packet.command == CFG_CMD_GET_PHY)
    {
        phy_status_t phy = {BleUart_IsConnected(), 0, 0, 0xff};

        BleUart_GetPhy(&phy.tx_phy, &phy.rx_phy, &phy.update_status);
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK,
                            &phy, sizeof(phy));
    }
    else if(packet.command == CFG_CMD_RESET)
    {
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK, NULL, 0);
        reset_pending = 1;
        reset_deadline = RTC_GetCounter() + 3200U;
    }
    else
        Config_MakeResponse(&packet, &packet, CFG_STATUS_BAD_COMMAND, NULL, 0);

    memcpy(response, &packet, sizeof(packet));
}

void ConfigService_Task(void)
{
    config_packet_t request;
    config_packet_t response;

    if(reset_pending && (int32_t)(RTC_GetCounter() - reset_deadline) >= 0)
        NVIC_SystemReset();

    if(UsbDevice_VendorRead((uint8_t *)&request, sizeof(request)) != sizeof(request))
        return;
    ConfigService_ProcessPacket((const uint8_t *)&request, (uint8_t *)&response);
    UsbDevice_VendorWrite((const uint8_t *)&response, sizeof(response));
}
