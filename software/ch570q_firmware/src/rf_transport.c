#include "CH57x_common.h"

#include "rf.h"
#include "rf_uart_rx.h"
#include "rf_uart_tx.h"
#include "uart.h"
#include "uart_bridge.h"
#include "usb_uart.h"
#include "usb_device.h"

#include "rf_legacy_port.h"
#include "config_protocol.h"
#include "rf_transport.h"

#include <string.h>

extern uint8_t slaveBoundStatus;
extern uint16_t slaveServerData;
extern rfipTx_t gTxParam;

static rf_transport_role_t current_role;
static uint8_t usb_pending[64];
static uint8_t usb_pending_length;

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
} remote_info_t;

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
} remote_status_t;

typedef struct __attribute__((packed))
{
    uint8_t tx_power;
    uint8_t reserved[3];
} remote_config_t;

static void remote_task(void)
{
    __attribute__((aligned(4))) uint8_t data[RF_REMOTE_PAYLOAD_SIZE];
    uint8_t transaction;
    uint8_t command;
    uint8_t length;
    uint8_t response_length = 0;
    uint8_t status = CFG_STATUS_OK;
    uint8_t reset_after_ack = 0;

    if(!RF_RemoteTakeRequest(&transaction, &command, data, &length))
        return;

    if(command == CFG_CMD_REMOTE_GET_INFO)
    {
        remote_info_t *info = (remote_info_t *)data;
        __attribute__((aligned(4))) uint8_t id[8];
        uint16_t id_sum;

        memset(info, 0, sizeof(*info));
        info->fw_minor = 4;
        info->fw_patch = 6;
        info->hardware = 0x72;
        info->rf_ready = 1;
        info->protocol = CFG_PROTOCOL_VERSION;
        info->system_clock = GetSysClock();
        info->app_start = IAP_APP_START;
        info->app_end = IAP_APP_END;
        info->chip_id = R8_CHIP_ID;
        info->boot_enabled = (R8_GLOB_CFG_INFO & RB_CFG_BOOT_EN) != 0;
        GetMACAddress(id);
        memcpy(info->mac, id, sizeof(info->mac));
        id_sum = (uint16_t)(id[0] | id[1] << 8) +
                 (uint16_t)(id[2] | id[3] << 8) +
                 (uint16_t)(id[4] | id[5] << 8);
        id[6] = (uint8_t)id_sum;
        id[7] = (uint8_t)(id_sum >> 8);
        memcpy(info->unique_id, id, sizeof(info->unique_id));
        response_length = sizeof(*info);
    }
    else if(command == CFG_CMD_REMOTE_GET_STATUS)
    {
        remote_status_t *remote = (remote_status_t *)data;
        uint16_t server_data;

        memset(remote, 0, sizeof(*remote));
        remote->rf_ready = 1;
        remote->rf_role = RF_TRANSPORT_UART_NODE;
        remote->uart_baud = UartBridge_GetBaud();
        remote->rf_uart_baud = RfTransport_GetUartBaud();
        RfTransport_GetPairStatus(&remote->paired, &remote->pair_state,
                                  &server_data);
        remote->server_data = server_data;
        response_length = sizeof(*remote);
    }
    else if(command == CFG_CMD_REMOTE_GET_CONFIG)
    {
        remote_config_t *config = (remote_config_t *)data;

        config->tx_power = RfTransport_GetTxPower();
        memset(config->reserved, 0, sizeof(config->reserved));
        response_length = sizeof(*config);
    }
    else if(command == CFG_CMD_REMOTE_SET_CONFIG)
    {
        if(length != sizeof(remote_config_t))
            status = CFG_STATUS_BAD_LENGTH;
        else
            RfTransport_SetTxPower(((remote_config_t *)data)->tx_power);
    }
    else if(command == CFG_CMD_REMOTE_RESET)
    {
        reset_after_ack = 1;
    }
    else
    {
        status = CFG_STATUS_BAD_COMMAND;
    }

    RF_RemoteSetResponse(transaction, command, status, data,
                         response_length, reset_after_ack);
}

static uint8_t set_role(rf_transport_role_t role, uint8_t usb_active)
{
    if(role != RF_TRANSPORT_USB_RECEIVER &&
       role != RF_TRANSPORT_UART_NODE)
        return 1;

    if(current_role != RF_TRANSPORT_OFF)
        RFRole_Stop();

    current_role = role;
    usb_pending_length = 0;

    RFRole_Init();
    if(role == RF_TRANSPORT_USB_RECEIVER)
    {
        RF_UartRxInit();
    }
    else if(role == RF_TRANSPORT_UART_NODE)
    {
        RfLegacyPort_Init();
        if(!usb_active)
        {
            GPIOA_SetBits(RTS | DTR);
            GPIOA_ModeCfg(RTS | DTR, GPIO_ModeOut_PP_5mA);
        }
        RF_UartTxInit();
    }
    return 0;
}

void RfTransport_Init(rf_transport_role_t role, uint8_t usb_active)
{
    current_role = RF_TRANSPORT_OFF;
    set_role(role, usb_active);
}

void RfTransport_Task(void)
{
    if(current_role == RF_TRANSPORT_USB_RECEIVER)
    {
        if(usb_pending_length)
        {
            if(UsbDevice_RfWrite(usb_pending, usb_pending_length))
                usb_pending_length = 0;
        }
        else
        {
            typeBufSize length = sizeof(usb_pending);
            if(RF_RxQuery(usb_pending, &length) && length)
            {
                usb_pending_length = (uint8_t)length;
                if(UsbDevice_RfWrite(usb_pending, usb_pending_length))
                    usb_pending_length = 0;
            }
        }
    }
    else if(current_role == RF_TRANSPORT_UART_NODE)
    {
        RfLegacyPort_Task();
        remote_task();
        RF_StatusQuery();
        if(RF_RemoteResetReady())
            SYS_ResetExecute();
    }
}

rf_transport_role_t RfTransport_GetRole(void)
{
    return current_role;
}

void RfTransport_GetPairStatus(uint8_t *paired, uint8_t *state,
                               uint16_t *server_data)
{
    if(current_role == RF_TRANSPORT_USB_RECEIVER)
    {
        *state = slaveBoundStatus;
        *paired = slaveBoundStatus == BOUND_STATUS_EST;
        *server_data = slaveServerData;
    }
    else if(current_role == RF_TRANSPORT_UART_NODE)
    {
        RF_GetPairStatus(paired, state, server_data);
    }
    else
    {
        *paired = 0;
        *state = 0;
        *server_data = 0;
    }
}

uint8_t RfTransport_StartPairing(void)
{
    if(current_role != RF_TRANSPORT_USB_RECEIVER)
        return 1;

    RFRole_Stop();
    rf_tx_set_sync_word(AA);
    rf_tx_set_frequency(DEF_FREQUENCY);
    rf_tx_set_phy_type(CONN_PHY_TYPE);
    rf_rx_set_sync_word(AA);
    rf_rx_set_frequency(DEF_FREQUENCY);
    rf_rx_set_phy_type(CONN_PHY_TYPE);
    RF_UartRxInit();
    return 0;
}

uint8_t RfTransport_ClearPairing(void)
{
    uint8_t result = RF_ClearPairing();

    if(!result && current_role == RF_TRANSPORT_USB_RECEIVER)
        RfTransport_StartPairing();
    return result;
}

uint8_t RfTransport_StartScan(void)
{
    if(current_role != RF_TRANSPORT_USB_RECEIVER)
        return 1;

    RFRole_Stop();
    rf_tx_set_sync_word(AA);
    rf_tx_set_frequency(DEF_FREQUENCY);
    rf_tx_set_phy_type(CONN_PHY_TYPE);
    rf_rx_set_sync_word(AA);
    rf_rx_set_frequency(DEF_FREQUENCY);
    rf_rx_set_phy_type(CONN_PHY_TYPE);
    RF_UartRxInit();
    RF_ScanStart();
    return 0;
}

uint8_t RfTransport_IsScanning(void)
{
    return current_role == RF_TRANSPORT_USB_RECEIVER && RF_ScanIsActive();
}

uint8_t RfTransport_GetScanResults(rf_scan_device_t *devices,
                                   uint8_t capacity)
{
    if(current_role != RF_TRANSPORT_USB_RECEIVER)
        return 0;
    return RF_ScanGetResults(devices, capacity);
}

uint8_t RfTransport_PairDevice(const uint8_t device_id[6])
{
    if(current_role != RF_TRANSPORT_USB_RECEIVER || !RF_ScanIsActive())
        return 1;
    return RF_ScanSelect(device_id);
}

uint8_t RfTransport_GetTxPower(void)
{
    return (uint8_t)gTxParam.txPowerVal;
}

void RfTransport_SetTxPower(uint8_t value)
{
    gTxParam.txPowerVal = (int8_t)value;
}

uint32_t RfTransport_GetUartBaud(void)
{
    return RfUartPara.BaudRate;
}

uint8_t RfTransport_RemoteCommand(uint8_t command, const uint8_t *request,
                                  uint8_t request_length, uint8_t *status,
                                  uint8_t *response,
                                  uint8_t *response_length)
{
    if(current_role != RF_TRANSPORT_USB_RECEIVER ||
       slaveBoundStatus != BOUND_STATUS_EST)
        return RF_REMOTE_EXCHANGE_BUSY;

    return RF_RemoteExchange(command, request, request_length, status,
                             response, response_length);
}
