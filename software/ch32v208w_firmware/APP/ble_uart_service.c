#include "ble_uart_service.h"

#define BLE_UART_TX_VALUE_ATTRIBUTE 2
#define BLE_UART_TX_CCCD_ATTRIBUTE 3
#define BLE_UART_RX_VALUE_ATTRIBUTE 5
#define BLE_UART_CONFIG_VALUE_ATTRIBUTE 7
#define BLE_UART_CONFIG_CCCD_ATTRIBUTE 8

static const uint8_t service_uuid[ATT_BT_UUID_SIZE] = {0xf0, 0xff};
static const uint8_t tx_uuid[ATT_BT_UUID_SIZE] = {0xf1, 0xff};
static const uint8_t rx_uuid[ATT_BT_UUID_SIZE] = {0xf2, 0xff};
static const uint8_t config_uuid[ATT_BT_UUID_SIZE] = {0xf3, 0xff};

static const gattAttrType_t service = {ATT_BT_UUID_SIZE, service_uuid};
static uint8_t tx_properties = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8_t rx_properties = GATT_PROP_WRITE_NO_RSP | GATT_PROP_WRITE;
static uint8_t config_properties = GATT_PROP_READ | GATT_PROP_WRITE |
                                   GATT_PROP_NOTIFY;
static uint8_t tx_value;
static uint8_t rx_value;
static uint8_t config_response[12] = {0x80, 0x00, 0x01, 0x00};
static uint8_t config_response_length = 4;
static gattCharCfg_t tx_cccd[4];
static gattCharCfg_t config_cccd[4];
static ble_uart_callback_t app_callback;

static bStatus_t read_attribute(uint16_t conn_handle,
                                gattAttribute_t *attribute,
                                uint8_t *value, uint16_t *length,
                                uint16_t offset, uint16_t max_length,
                                uint8_t method);
static bStatus_t write_attribute(uint16_t conn_handle,
                                 gattAttribute_t *attribute,
                                 uint8_t *value, uint16_t length,
                                 uint16_t offset, uint8_t method);
static void connection_changed(uint16_t conn_handle, uint8_t change_type);

static gattAttribute_t attributes[] = {
    {{ATT_BT_UUID_SIZE, primaryServiceUUID}, GATT_PERMIT_READ, 0,
     (uint8_t *)&service},
    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &tx_properties},
    {{ATT_BT_UUID_SIZE, tx_uuid}, GATT_PERMIT_READ, 0, &tx_value},
    {{ATT_BT_UUID_SIZE, clientCharCfgUUID},
     GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, (uint8_t *)tx_cccd},
    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &rx_properties},
    {{ATT_BT_UUID_SIZE, rx_uuid}, GATT_PERMIT_WRITE, 0, &rx_value},
    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &config_properties},
    {{ATT_BT_UUID_SIZE, config_uuid},
     GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, config_response},
    {{ATT_BT_UUID_SIZE, clientCharCfgUUID},
     GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, (uint8_t *)config_cccd}
};

static gattServiceCBs_t callbacks = {
    read_attribute,
    write_attribute,
    NULL
};

bStatus_t BleUartService_Add(ble_uart_callback_t callback)
{
    app_callback = callback;
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, tx_cccd);
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, config_cccd);
    linkDB_Register(connection_changed);
    return GATTServApp_RegisterService(attributes,
                                       GATT_NUM_ATTRS(attributes),
                                       GATT_MAX_ENCRYPT_KEY_SIZE,
                                       &callbacks);
}

static bStatus_t read_attribute(uint16_t conn_handle,
                                gattAttribute_t *attribute,
                                uint8_t *value, uint16_t *length,
                                uint16_t offset, uint16_t max_length,
                                uint8_t method)
{
    uint16_t uuid;

    (void)method;
    if(attribute->type.len != ATT_BT_UUID_SIZE)
        return ATT_ERR_ATTR_NOT_FOUND;

    uuid = BUILD_UINT16(attribute->type.uuid[0], attribute->type.uuid[1]);
    if(uuid == 0xfff1U)
    {
        if(offset != 0U)
            return ATT_ERR_ATTR_NOT_LONG;
        value[0] = tx_value;
        *length = 1;
        return SUCCESS;
    }

    if(uuid == 0xfff3U)
    {
        uint16_t remaining;

        if(offset >= config_response_length)
            return ATT_ERR_ATTR_NOT_LONG;
        remaining = config_response_length - offset;
        if(remaining > max_length)
            remaining = max_length;
        tmos_memcpy(value, config_response + offset, remaining);
        *length = remaining;
        return SUCCESS;
    }

    if(offset != 0U)
        return ATT_ERR_ATTR_NOT_LONG;
    if(uuid != GATT_CLIENT_CHAR_CFG_UUID)
        return ATT_ERR_ATTR_NOT_FOUND;

    {
        gattCharCfg_t *cccd =
            attribute->handle == attributes[BLE_UART_TX_CCCD_ATTRIBUTE].handle
            ? tx_cccd : config_cccd;
        uint16_t config = GATTServApp_ReadCharCfg(conn_handle, cccd);

        value[0] = LO_UINT16(config);
        value[1] = HI_UINT16(config);
    }
    *length = 2;
    return SUCCESS;
}

static bStatus_t write_attribute(uint16_t conn_handle,
                                 gattAttribute_t *attribute,
                                 uint8_t *value, uint16_t length,
                                 uint16_t offset, uint8_t method)
{
    uint16_t uuid;

    (void)method;
    if(gattPermitAuthorWrite(attribute->permissions))
        return ATT_ERR_INSUFFICIENT_AUTHOR;
    if(attribute->type.len != ATT_BT_UUID_SIZE)
        return ATT_ERR_ATTR_NOT_FOUND;

    uuid = BUILD_UINT16(attribute->type.uuid[0], attribute->type.uuid[1]);
    if(uuid == GATT_CLIENT_CHAR_CFG_UUID)
    {
        uint8_t is_tx_cccd =
            attribute->handle == attributes[BLE_UART_TX_CCCD_ATTRIBUTE].handle;
        bStatus_t status = GATTServApp_ProcessCCCWriteReq(
            conn_handle, attribute, value, length, offset,
            GATT_CLIENT_CFG_NOTIFY);

        if(status == SUCCESS && app_callback)
        {
            ble_uart_evt_t event;
            uint16_t config = BUILD_UINT16(value[0], value[1]);

            if(is_tx_cccd)
            {
                event.type = config == GATT_CFG_NO_OPERATION
                           ? BLE_UART_EVT_TX_NOTI_DISABLED
                           : BLE_UART_EVT_TX_NOTI_ENABLED;
            }
            else if(config != GATT_CFG_NO_OPERATION)
            {
                event.type = BLE_UART_EVT_CONFIG_NOTI_ENABLED;
            }
            else
            {
                return status;
            }
            event.data = NULL;
            event.length = 0;
            (void)app_callback(conn_handle, &event);
        }
        return status;
    }

    if(attribute->handle == attributes[BLE_UART_RX_VALUE_ATTRIBUTE].handle)
    {
        if(app_callback)
        {
            const ble_uart_evt_t event = {
                BLE_UART_EVT_DATA_RECEIVED, value, length
            };
            (void)app_callback(conn_handle, &event);
        }
        return SUCCESS;
    }

    if(attribute->handle == attributes[BLE_UART_CONFIG_VALUE_ATTRIBUTE].handle)
    {
        ble_uart_evt_t event = {
            BLE_UART_EVT_CONFIG_RECEIVED, value, length
        };

        if(offset != 0U)
            return ATT_ERR_ATTR_NOT_LONG;
        if(length == 0U || length > sizeof(config_response))
            return ATT_ERR_INVALID_VALUE_SIZE;

        tmos_memcpy(config_response, value, length);
        config_response[0] |= 0x80U;
        config_response_length = length;
        if(app_callback)
            (void)app_callback(conn_handle, &event);
        return SUCCESS;
    }

    return ATT_ERR_ATTR_NOT_FOUND;
}

static void connection_changed(uint16_t conn_handle, uint8_t change_type)
{
    if(conn_handle != LOOPBACK_CONNHANDLE &&
       (change_type == LINKDB_STATUS_UPDATE_REMOVED ||
        (change_type == LINKDB_STATUS_UPDATE_STATEFLAGS &&
         !linkDB_Up(conn_handle))))
    {
        GATTServApp_InitCharCfg(conn_handle, tx_cccd);
        GATTServApp_InitCharCfg(conn_handle, config_cccd);
    }
}

uint8_t BleUartService_NotifyReady(uint16_t conn_handle)
{
    return GATTServApp_ReadCharCfg(conn_handle, tx_cccd) ==
           GATT_CLIENT_CFG_NOTIFY;
}

bStatus_t BleUartService_Notify(uint16_t conn_handle,
                                attHandleValueNoti_t *notification)
{
    if(!BleUartService_NotifyReady(conn_handle))
        return bleIncorrectMode;

    notification->handle = attributes[BLE_UART_TX_VALUE_ATTRIBUTE].handle;
    return GATT_Notification(conn_handle, notification, FALSE);
}

uint8_t BleUartService_ConfigNotifyReady(uint16_t conn_handle)
{
    return GATTServApp_ReadCharCfg(conn_handle, config_cccd) ==
           GATT_CLIENT_CFG_NOTIFY;
}

bStatus_t BleUartService_NotifyStatus(uint16_t conn_handle,
                                      uint8_t flags,
                                      uint8_t modem_status)
{
    attHandleValueNoti_t notification;
    bStatus_t status;
    uint8_t uart_status[7] = {
        0x88, 0x00, 0x04, 0x00, flags, modem_status, 0x00
    };

    if(!BleUartService_ConfigNotifyReady(conn_handle))
        return bleIncorrectMode;

    uart_status[6] = uart_status[3] + uart_status[4] + uart_status[5];
    notification.len = sizeof(uart_status);
    notification.pValue = GATT_bm_alloc(conn_handle,
                                         ATT_HANDLE_VALUE_NOTI,
                                         notification.len, NULL, 0);
    if(!notification.pValue)
        return bleMemAllocError;

    tmos_memcpy(notification.pValue, uart_status, sizeof(uart_status));
    notification.handle =
        attributes[BLE_UART_CONFIG_VALUE_ATTRIBUTE].handle;
    status = GATT_Notification(conn_handle, &notification, FALSE);
    if(status != SUCCESS)
        GATT_bm_free((gattMsg_t *)&notification, ATT_HANDLE_VALUE_NOTI);
    return status;
}
