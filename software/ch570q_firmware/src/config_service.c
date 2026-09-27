#include "CH57x_common.h"

#include "config_protocol.h"
#include "config_service.h"
#include "rf_transport.h"
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
    uint8_t rf_ready;
    uint8_t protocol;
    uint8_t reserved;
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
    uint8_t rf_ready;
    uint8_t rf_role;
    uint8_t paired;
    uint8_t pair_state;
    uint8_t reserved;
    uint16_t server_data;
    uint32_t uart_baud;
    uint32_t rf_uart_baud;
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
} rf_config_t;

typedef struct __attribute__((packed))
{
    uint8_t active;
    uint8_t count;
    uint16_t reserved;
    rf_scan_device_t devices[RF_SCAN_MAX_DEVICES];
} scan_results_t;

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

void ConfigService_Task(void)
{
    config_packet_t packet;

    if((reset_pending || isp_pending) &&
       (int32_t)(SYS_GetSysTickCnt() - reset_time) >= 0)
    {
        if(isp_pending)
            enter_isp_boot();
        SYS_ResetExecute();
    }

    if(UsbDevice_VendorRead((uint8_t *)&packet, sizeof(packet)) != sizeof(packet))
        return;

    if(!Config_PacketValid(&packet))
    {
        memset(&packet, 0, sizeof(packet));
        packet.command = 0x7f;
        Config_MakeResponse(&packet, &packet, CFG_STATUS_BAD_PACKET, 0, 0);
    }
    else if(packet.command == CFG_CMD_GET_INFO)
    {
        app_info_t info = {
            0, 4, 6, 0x72, 2, 1, CFG_PROTOCOL_VERSION, 0,
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
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK,
                            &info, sizeof(info));
    }
    else if(packet.command == CFG_CMD_GET_STATUS)
    {
        app_status_t status;
        uint16_t server_data;
        memset(&status, 0, sizeof(status));
        status.usb_configured = UsbDevice_IsConfigured();
        status.rf_ready = RfTransport_GetRole() != RF_TRANSPORT_OFF;
        status.rf_role = RfTransport_GetRole();
        status.uart_baud = UartBridge_GetBaud();
        status.rf_uart_baud = RfTransport_GetUartBaud();
        RfTransport_GetPairStatus(&status.paired, &status.pair_state,
                                  &server_data);
        status.server_data = server_data;
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK,
                            &status, sizeof(status));
    }
    else if(packet.command == CFG_CMD_GET_PAIR)
    {
        pair_status_t pair;
        uint16_t server_data;
        RfTransport_GetPairStatus(&pair.paired, &pair.pair_state,
                                  &server_data);
        pair.server_data = server_data;
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK,
                            &pair, sizeof(pair));
    }
    else if(packet.command == CFG_CMD_START_PAIR)
    {
        uint8_t result = RfTransport_StartPairing();
        Config_MakeResponse(&packet, &packet,
                            result ? CFG_STATUS_STATE : CFG_STATUS_OK, 0, 0);
    }
    else if(packet.command == CFG_CMD_CLEAR_PAIR)
    {
        uint8_t result = RfTransport_ClearPairing();
        Config_MakeResponse(&packet, &packet,
                            result ? CFG_STATUS_FLASH : CFG_STATUS_OK, 0, 0);
    }
    else if(packet.command == CFG_CMD_GET_RF_CONFIG)
    {
        const rf_config_t config = {RfTransport_GetTxPower(), {0, 0, 0}};
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK,
                            &config, sizeof(config));
    }
    else if(packet.command == CFG_CMD_SET_RF_CONFIG)
    {
        if(packet.length != sizeof(rf_config_t))
        {
            Config_MakeResponse(&packet, &packet,
                                CFG_STATUS_BAD_LENGTH, 0, 0);
        }
        else
        {
            const rf_config_t *config = (const rf_config_t *)packet.payload;
            RfTransport_SetTxPower(config->tx_power);
            Config_MakeResponse(&packet, &packet, CFG_STATUS_OK, 0, 0);
        }
    }
    else if(packet.command == CFG_CMD_START_SCAN)
    {
        uint8_t result = RfTransport_StartScan();
        Config_MakeResponse(&packet, &packet,
                            result ? CFG_STATUS_STATE : CFG_STATUS_OK, 0, 0);
    }
    else if(packet.command == CFG_CMD_GET_SCAN)
    {
        scan_results_t results;
        memset(&results, 0, sizeof(results));
        results.active = RfTransport_IsScanning();
        results.count = RfTransport_GetScanResults(results.devices,
                                                    RF_SCAN_MAX_DEVICES);
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK,
                            &results, sizeof(results));
    }
    else if(packet.command == CFG_CMD_PAIR_DEVICE)
    {
        if(packet.length != 6U)
        {
            Config_MakeResponse(&packet, &packet,
                                CFG_STATUS_BAD_LENGTH, 0, 0);
        }
        else
        {
            uint8_t result = RfTransport_PairDevice(packet.payload);
            Config_MakeResponse(&packet, &packet,
                                result ? CFG_STATUS_STATE : CFG_STATUS_OK,
                                0, 0);
        }
    }
    else if(packet.command == CFG_CMD_RESET)
    {
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK, 0, 0);
        reset_pending = 1;
        reset_time = SYS_GetSysTickCnt() + GetSysClock() / 10U;
    }
    else if(packet.command == CFG_CMD_ENTER_ISP)
    {
        Config_MakeResponse(&packet, &packet, CFG_STATUS_OK, 0, 0);
        isp_pending = 1;
        reset_time = SYS_GetSysTickCnt() + GetSysClock() / 10U;
    }
    else if(packet.command >= CFG_CMD_REMOTE_GET_INFO &&
            packet.command <= CFG_CMD_REMOTE_RESET)
    {
        uint8_t remote_status = CFG_STATUS_OK;
        uint8_t remote_length = CFG_PAYLOAD_SIZE;
        uint8_t result;

        if(packet.length > RF_TRANSPORT_REMOTE_PAYLOAD_SIZE)
        {
            Config_MakeResponse(&packet, &packet,
                                CFG_STATUS_BAD_LENGTH, 0, 0);
        }
        else
        {
            result = RfTransport_RemoteCommand(
                packet.command, packet.payload, (uint8_t)packet.length,
                &remote_status, packet.payload, &remote_length);
            if(result == RF_REMOTE_EXCHANGE_COMPLETE)
            {
                Config_MakeResponse(&packet, &packet, remote_status,
                                    packet.payload, remote_length);
            }
            else if(result == RF_REMOTE_EXCHANGE_PENDING)
            {
                Config_MakeResponse(&packet, &packet,
                                    CFG_STATUS_PENDING, 0, 0);
            }
            else if(result == RF_REMOTE_EXCHANGE_TIMEOUT)
            {
                Config_MakeResponse(&packet, &packet,
                                    CFG_STATUS_TIMEOUT, 0, 0);
            }
            else
            {
                Config_MakeResponse(&packet, &packet,
                                    CFG_STATUS_STATE, 0, 0);
            }
        }
    }
    else
    {
        Config_MakeResponse(&packet, &packet, CFG_STATUS_BAD_COMMAND, 0, 0);
    }

    UsbDevice_VendorWrite((uint8_t *)&packet, sizeof(packet));
}
