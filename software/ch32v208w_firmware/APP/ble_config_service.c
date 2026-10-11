#include "ble_config_service.h"

#define CONFIG_TX_VALUE_ATTRIBUTE 2
#define CONFIG_RX_VALUE_ATTRIBUTE 5

static const uint8_t service_uuid[ATT_BT_UUID_SIZE] = {0xe0, 0xff};
static const uint8_t tx_uuid[ATT_BT_UUID_SIZE] = {0xe1, 0xff};
static const uint8_t rx_uuid[ATT_BT_UUID_SIZE] = {0xe2, 0xff};

static const gattAttrType_t service = {ATT_BT_UUID_SIZE, service_uuid};
static uint8_t tx_properties = GATT_PROP_NOTIFY;
static uint8_t rx_properties = GATT_PROP_WRITE_NO_RSP | GATT_PROP_WRITE;
static uint8_t tx_value;
static uint8_t rx_value;
static gattCharCfg_t tx_cccd[4];
static ble_config_callback_t app_callback;

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
    {{ATT_BT_UUID_SIZE, tx_uuid}, 0, 0, &tx_value},
    {{ATT_BT_UUID_SIZE, clientCharCfgUUID},
     GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, (uint8_t *)tx_cccd},
    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &rx_properties},
    {{ATT_BT_UUID_SIZE, rx_uuid}, GATT_PERMIT_WRITE, 0, &rx_value}
};

static gattServiceCBs_t callbacks = {
    read_attribute,
    write_attribute,
    NULL
};

bStatus_t BleConfigService_Add(ble_config_callback_t callback)
{
    app_callback = callback;
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, tx_cccd);
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

    (void)max_length;
    (void)method;
    if(offset != 0U)
        return ATT_ERR_ATTR_NOT_LONG;
    if(attribute->type.len != ATT_BT_UUID_SIZE)
        return ATT_ERR_ATTR_NOT_FOUND;

    uuid = BUILD_UINT16(attribute->type.uuid[0], attribute->type.uuid[1]);
    if(uuid != GATT_CLIENT_CHAR_CFG_UUID)
        return ATT_ERR_ATTR_NOT_FOUND;

    {
        uint16_t config = GATTServApp_ReadCharCfg(conn_handle, tx_cccd);

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
        return GATTServApp_ProcessCCCWriteReq(conn_handle, attribute,
                                              value, length, offset,
                                              GATT_CLIENT_CFG_NOTIFY);
    }

    if(attribute->handle == attributes[CONFIG_RX_VALUE_ATTRIBUTE].handle)
    {
        if(app_callback)
            app_callback(value, length);
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
    }
}

uint8_t BleConfigService_NotifyReady(uint16_t conn_handle)
{
    return GATTServApp_ReadCharCfg(conn_handle, tx_cccd) ==
           GATT_CLIENT_CFG_NOTIFY;
}

bStatus_t BleConfigService_Notify(uint16_t conn_handle,
                                  attHandleValueNoti_t *notification)
{
    if(!BleConfigService_NotifyReady(conn_handle))
        return bleIncorrectMode;

    notification->handle = attributes[CONFIG_TX_VALUE_ATTRIBUTE].handle;
    return GATT_Notification(conn_handle, notification, FALSE);
}
