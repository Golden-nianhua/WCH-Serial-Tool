#include "CONFIG.h"

#include "ble_device_info.h"
#include "ble_uart_app.h"
#include "ble_uart_service.h"
#include "uart_bridge.h"

#define BLE_UART_START_EVENT       0x0001
#define BLE_UART_SEND_EVENT        0x0002
#define BLE_UART_READ_RSSI_EVENT   0x0004
#define BLE_UART_STATUS_EVENT      0x0010
#define BLE_PARAM_UPDATE_EVENT     0x0020
#define BLE_UART_RSSI_PERIOD       3200U
#define BLE_PARAM_UPDATE_DELAY     6400U
#define BLE_UART_ADV_INTERVAL      80U
#define BLE_UART_PREFERRED_MIN_INTERVAL 6U
#define BLE_UART_PREFERRED_MAX_INTERVAL 6U
#define BLE_UART_ALLOWED_MIN_INTERVAL   6U
#define BLE_UART_ALLOWED_MAX_INTERVAL   1000U
#define BLE_UART_NOTIFY_MAX         244U
#define BLE_UART_RX_PACKET_MAX      (BLE_BUFF_MAX_LEN - 7U)
#define BLE_UART_DATA_PACKETS_DUPLEX 2U
#define BLE_UART_DATA_PACKETS_SINGLE 4U
#define BLE_UART_DUPLEX_HOLD         MS1_TO_SYSTEM_TIME(100U)
#define BLE_UART_TX_RESUME_FREE     BLE_UART_RX_PACKET_MAX

static uint8_t task_id = INVALID_TASK_ID;
static uint16_t connection_handle = GAP_CONNHANDLE_INIT;
static uint8_t state = BLE_UART_STATE_STARTING;
static int8_t connection_rssi;
static uint8_t tx_power = BLE_TX_POWER;
static uint8_t uart_pending_data[BLE_UART_NOTIFY_MAX];
static uint16_t uart_pending_length;
static uint8_t uart_send_scheduled;
static uint8_t uart_ready = 1;
static uint8_t uart_status_pending;
static uint8_t uart_status_valid;
static uint8_t ble_rx_seen;
static uint32_t ble_rx_last_tick;

static uint8_t scan_response[] = {
    0x10, GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'W', 'C', 'H', ' ', 'S', 'e', 'r', 'i', 'a', 'l', ' ',
    'T', 'o', 'o', 'l',
    0x05, GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
    LO_UINT16(BLE_UART_PREFERRED_MIN_INTERVAL),
    HI_UINT16(BLE_UART_PREFERRED_MIN_INTERVAL),
    LO_UINT16(BLE_UART_PREFERRED_MAX_INTERVAL),
    HI_UINT16(BLE_UART_PREFERRED_MAX_INTERVAL),
    0x02, GAP_ADTYPE_POWER_LEVEL, 0
};

static uint8_t advertising_data[] = {
    0x02, GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
    0x05, GAP_ADTYPE_16BIT_COMPLETE,
    0xf0, 0xff, 0xe0, 0xff,
    0x0b, GAP_ADTYPE_MANUFACTURER_SPECIFIC,
    0x57, 0xab, 0x07, 0x01,
    'C', 'H', '9', '1', '4', '3'
};

static uint8_t device_name[GAP_DEVICE_NAME_LEN] = "WCH Serial Tool";

static uint16_t process_event(uint8_t id, uint16_t events);
static void state_changed(gapRole_States_t new_state, gapRoleEvent_t *event);
static void rssi_changed(uint16_t conn_handle, int8_t rssi);
static void params_changed(uint16_t conn_handle, uint16_t interval,
                           uint16_t latency, uint16_t timeout);
static uint8_t service_event(uint16_t conn_handle,
                             const ble_uart_evt_t *event);

static gapRolesCBs_t role_callbacks = {
    state_changed,
    rssi_changed,
    params_changed
};

static gapBondCBs_t bond_callbacks = {
    NULL,
    NULL,
    NULL
};

static void schedule_uart_send(uint16_t delay)
{
    if(uart_send_scheduled)
        return;

    uart_send_scheduled = 1;
    if(delay)
        tmos_start_task(task_id, BLE_UART_SEND_EVENT, delay);
    else
        tmos_set_event(task_id, BLE_UART_SEND_EVENT);
}

static void start_advertising(void)
{
    uint8_t enabled = TRUE;

    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(enabled), &enabled);
}

static void schedule_uart_status(uint8_t ready)
{
    if(ready && uart_status_pending && !uart_ready)
        return;
    if(uart_status_valid && ready == uart_ready)
        return;

    uart_ready = ready;
    uart_status_pending = 1;
    uart_status_valid = 1;
    tmos_set_event(task_id, BLE_UART_STATUS_EVENT);
}

void BleUart_Init(void)
{
    uint8_t advertising_enabled = TRUE;
    uint16_t minimum_interval = BLE_UART_ALLOWED_MIN_INTERVAL;
    uint16_t maximum_interval = BLE_UART_ALLOWED_MAX_INTERVAL;
    uint16_t advertising_interval = BLE_UART_ADV_INTERVAL;
    uint8_t pairing_mode = GAPBOND_PAIRING_MODE_NO_PAIRING;
    uint8_t supported_phy = GAP_PHY_BIT_LE_1M;

    task_id = TMOS_ProcessEventRegister(process_event);

    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED,
                         sizeof(advertising_enabled),
                         &advertising_enabled);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA,
                         sizeof(scan_response), scan_response);
    GAPRole_SetParameter(GAPROLE_ADVERT_DATA,
                         sizeof(advertising_data), advertising_data);
    GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL,
                         sizeof(minimum_interval), &minimum_interval);
    GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL,
                         sizeof(maximum_interval), &maximum_interval);
    GAPRole_SetParameter(GAPROLE_PHY_TX_SUPPORTED,
                         sizeof(supported_phy), &supported_phy);
    GAPRole_SetParameter(GAPROLE_PHY_RX_SUPPORTED,
                         sizeof(supported_phy), &supported_phy);
    GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advertising_interval);
    GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advertising_interval);
    GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE,
                            sizeof(pairing_mode), &pairing_mode);

    GGS_AddService(GATT_ALL_SERVICES);
    GATTServApp_AddService(GATT_ALL_SERVICES);
    BleDeviceInfo_Add();
    BleUartService_Add(service_event);
    GGS_SetParameter(GGS_DEVICE_NAME_ATT,
                     GAP_DEVICE_NAME_LEN, device_name);

    tmos_set_event(task_id, BLE_UART_START_EVENT);
}

static uint8_t packet_checksum_ok(const uint8_t *data, uint16_t length)
{
    uint8_t checksum = 0;
    uint16_t index;

    for(index = 3; index + 1U < length; index++)
        checksum += data[index];
    return checksum == data[length - 1U];
}

static uint8_t service_event(uint16_t conn_handle,
                             const ble_uart_evt_t *event)
{
    uint8_t status = 1;

    (void)conn_handle;

    if(event->type == BLE_UART_EVT_DATA_RECEIVED)
    {
        uint16_t free = UartBridge_TxFree();
        uint8_t accepted = 0;

        ble_rx_seen = 1;
        ble_rx_last_tick = TMOS_GetSystemClock();

        if(uart_pending_length == 0U && free >= event->length)
        {
            (void)UartBridge_Write(event->data, event->length);
            accepted = 1;
        }
        else if(uart_pending_length == 0U &&
                event->length <= sizeof(uart_pending_data))
        {
            tmos_memcpy(uart_pending_data, event->data, event->length);
            uart_pending_length = event->length;
            accepted = 1;
        }

        if(!accepted || uart_pending_length != 0U)
            schedule_uart_status(0);
    }
    else if(event->type == BLE_UART_EVT_TX_NOTI_ENABLED)
    {
        schedule_uart_send(0);
    }
    else if(event->type == BLE_UART_EVT_CONFIG_RECEIVED)
    {
        const uint8_t *data = event->data;

        if(event->length >= 4U &&
           packet_checksum_ok(data, event->length))
        {
            if(data[0] == 0x06U && event->length == 12U &&
               data[2] == 9U)
            {
                uint32_t baud = (uint32_t)data[4] |
                                ((uint32_t)data[5] << 8) |
                                ((uint32_t)data[6] << 16) |
                                ((uint32_t)data[7] << 24);

                status = UartBridge_Configure(baud, data[8],
                                               data[9], data[10]);
            }
            else if(data[0] == 0x07U && event->length == 8U &&
                    data[2] == 5U)
            {
                UartBridge_SetModem(data[6], data[5]);
                status = 0;
            }
        }
        return status;
    }
    else if(event->type == BLE_UART_EVT_CONFIG_NOTI_ENABLED)
    {
        schedule_uart_status(1);
    }

    return 0;
}

static void link_established(gapRoleEvent_t *event)
{
    gapEstLinkReqEvent_t *link = (gapEstLinkReqEvent_t *)event;

    if(connection_handle != GAP_CONNHANDLE_INIT)
    {
        GAPRole_TerminateLink(event->linkCmpl.connectionHandle);
        return;
    }

    connection_handle = link->connectionHandle;
    state = BLE_UART_STATE_CONNECTED;
    connection_rssi = 0;
    uart_pending_length = 0;
    uart_send_scheduled = 0;
    uart_ready = 1;
    uart_status_pending = 0;
    uart_status_valid = 0;
    ble_rx_seen = 0;
    tmos_start_task(task_id, BLE_UART_READ_RSSI_EVENT,
                    BLE_UART_RSSI_PERIOD);
    tmos_start_task(task_id, BLE_PARAM_UPDATE_EVENT,
                    BLE_PARAM_UPDATE_DELAY);
}

static void link_terminated(gapRoleEvent_t *event)
{
    uint8_t discarded;

    if(event->linkTerminate.connectionHandle != connection_handle)
        return;

    connection_handle = GAP_CONNHANDLE_INIT;
    state = BLE_UART_STATE_ADVERTISING;
    uart_pending_length = 0;
    uart_send_scheduled = 0;
    uart_status_pending = 0;
    uart_status_valid = 0;
    ble_rx_seen = 0;
    UartBridge_SetModem(0, 0);
    tmos_stop_task(task_id, BLE_UART_READ_RSSI_EVENT);
    tmos_stop_task(task_id, BLE_UART_SEND_EVENT);
    tmos_stop_task(task_id, BLE_UART_STATUS_EVENT);
    tmos_stop_task(task_id, BLE_PARAM_UPDATE_EVENT);
    while(UartBridge_ReadByte(&discarded))
    {
    }
    start_advertising();
}

static void state_changed(gapRole_States_t new_state, gapRoleEvent_t *event)
{
    switch(new_state & GAPROLE_STATE_ADV_MASK)
    {
        case GAPROLE_STARTED:
            state = BLE_UART_STATE_ADVERTISING;
            break;
        case GAPROLE_ADVERTISING:
            state = BLE_UART_STATE_ADVERTISING;
            if(event && event->gap.opcode == GAP_LINK_TERMINATED_EVENT)
                link_terminated(event);
            break;
        case GAPROLE_CONNECTED:
            if(event && event->gap.opcode == GAP_LINK_ESTABLISHED_EVENT)
                link_established(event);
            break;
        case GAPROLE_WAITING:
            if(event && event->gap.opcode == GAP_LINK_TERMINATED_EVENT)
                link_terminated(event);
            else
                start_advertising();
            break;
        default:
            break;
    }
}

static void rssi_changed(uint16_t conn_handle, int8_t rssi)
{
    if(conn_handle == connection_handle)
        connection_rssi = rssi;
}

static void params_changed(uint16_t conn_handle, uint16_t interval,
                           uint16_t latency, uint16_t timeout)
{
    (void)conn_handle;
    (void)interval;
    (void)latency;
    (void)timeout;
}

static void send_notifications(void)
{
    uint8_t packets = 0;
    uint8_t packet_limit = BLE_UART_DATA_PACKETS_SINGLE;

    uart_send_scheduled = 0;
    if(connection_handle == GAP_CONNHANDLE_INIT ||
       !BleUartService_NotifyReady(connection_handle))
        return;

    if(uart_pending_length ||
       (ble_rx_seen &&
        (uint32_t)(TMOS_GetSystemClock() - ble_rx_last_tick) <
        BLE_UART_DUPLEX_HOLD))
        packet_limit = BLE_UART_DATA_PACKETS_DUPLEX;

    while(packets < packet_limit)
    {
        attHandleValueNoti_t notification;
        uint16_t length = UartBridge_Available();
        uint16_t mtu_payload = ATT_GetMTU(connection_handle) - 3U;
        bStatus_t result;

        if(length == 0U)
            return;
        if(mtu_payload > BLE_UART_NOTIFY_MAX)
            mtu_payload = BLE_UART_NOTIFY_MAX;
        if(length > mtu_payload)
            length = mtu_payload;

        notification.len = length;
        notification.pValue = GATT_bm_alloc(connection_handle,
                                             ATT_HANDLE_VALUE_NOTI,
                                             length, NULL, 0);
        if(!notification.pValue)
        {
            schedule_uart_send(2);
            return;
        }

        notification.len = UartBridge_Peek(notification.pValue, length);
        result = BleUartService_Notify(connection_handle, &notification);
        if(result != SUCCESS)
        {
            GATT_bm_free((gattMsg_t *)&notification,
                         ATT_HANDLE_VALUE_NOTI);
            schedule_uart_send(2);
            return;
        }

        UartBridge_Consume(length);
        packets++;
    }

    if(UartBridge_Available())
        schedule_uart_send(1);
}

static void send_uart_status(void)
{
    bStatus_t result;

    if(!uart_status_pending ||
       connection_handle == GAP_CONNHANDLE_INIT ||
       !BleUartService_ConfigNotifyReady(connection_handle))
        return;

    result = BleUartService_NotifyStatus(connection_handle,
                                         uart_ready ? 0x01U : 0x04U,
                                         0x00U);
    if(result == SUCCESS)
    {
        uart_status_pending = 0;
    }
    else
    {
        tmos_start_task(task_id, BLE_UART_STATUS_EVENT, 2);
    }
}

static uint16_t process_event(uint8_t id, uint16_t events)
{
    (void)id;
    if(events & SYS_EVENT_MSG)
    {
        uint8_t *message = tmos_msg_receive(task_id);

        if(message)
            tmos_msg_deallocate(message);
        return events ^ SYS_EVENT_MSG;
    }

    if(events & BLE_UART_START_EVENT)
    {
        GAPRole_PeripheralStartDevice(task_id,
                                      &bond_callbacks,
                                      &role_callbacks);
        return events ^ BLE_UART_START_EVENT;
    }

    if(events & BLE_UART_STATUS_EVENT)
    {
        send_uart_status();
        return events ^ BLE_UART_STATUS_EVENT;
    }

    if(events & BLE_UART_SEND_EVENT)
    {
        send_notifications();
        return events ^ BLE_UART_SEND_EVENT;
    }

    if(events & BLE_UART_READ_RSSI_EVENT)
    {
        if(connection_handle != GAP_CONNHANDLE_INIT)
        {
            GAPRole_ReadRssiCmd(connection_handle);
            tmos_start_task(task_id, BLE_UART_READ_RSSI_EVENT,
                            BLE_UART_RSSI_PERIOD);
        }
        return events ^ BLE_UART_READ_RSSI_EVENT;
    }

    if(events & BLE_PARAM_UPDATE_EVENT)
    {
        if(connection_handle != GAP_CONNHANDLE_INIT)
        {
            GAPRole_PeripheralConnParamUpdateReq(
                connection_handle,
                BLE_UART_PREFERRED_MIN_INTERVAL,
                BLE_UART_PREFERRED_MAX_INTERVAL,
                0U, 100U, task_id);
        }
        return events ^ BLE_PARAM_UPDATE_EVENT;
    }

    return 0;
}

void BleUart_Poll(void)
{
    if(uart_pending_length &&
       UartBridge_TxFree() >= uart_pending_length)
    {
        (void)UartBridge_Write(uart_pending_data, uart_pending_length);
        uart_pending_length = 0;
    }

    if(connection_handle != GAP_CONNHANDLE_INIT &&
       !uart_ready &&
       uart_pending_length == 0U &&
       UartBridge_TxFree() >= BLE_UART_TX_RESUME_FREE)
    {
        schedule_uart_status(1);
    }

    if(connection_handle != GAP_CONNHANDLE_INIT &&
       BleUartService_NotifyReady(connection_handle) &&
       UartBridge_Available())
    {
        schedule_uart_send(0);
    }
}

uint8_t BleUart_IsConnected(void)
{
    return connection_handle != GAP_CONNHANDLE_INIT;
}

uint8_t BleUart_GetState(void)
{
    return state;
}

int8_t BleUart_GetRssi(void)
{
    return connection_rssi;
}

uint8_t BleUart_GetTxPower(void)
{
    return tx_power;
}

uint8_t BleUart_SetTxPower(uint8_t power)
{
    bStatus_t result = LL_SetTxPowerLevel(power);

    if(result == SUCCESS)
        tx_power = power;
    return result;
}
