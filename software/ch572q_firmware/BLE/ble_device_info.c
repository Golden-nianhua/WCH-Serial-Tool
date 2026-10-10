#include "ble_device_info.h"

#define FLASH_CONST __attribute__((section(".rodata")))

static const uint8_t service_uuid[ATT_BT_UUID_SIZE] FLASH_CONST = {
    LO_UINT16(DEVINFO_SERV_UUID), HI_UINT16(DEVINFO_SERV_UUID)
};
static const uint8_t model_uuid[ATT_BT_UUID_SIZE] FLASH_CONST = {
    LO_UINT16(MODEL_NUMBER_UUID), HI_UINT16(MODEL_NUMBER_UUID)
};
static const uint8_t serial_uuid[ATT_BT_UUID_SIZE] FLASH_CONST = {
    LO_UINT16(SERIAL_NUMBER_UUID), HI_UINT16(SERIAL_NUMBER_UUID)
};
static const uint8_t firmware_uuid[ATT_BT_UUID_SIZE] FLASH_CONST = {
    LO_UINT16(FIRMWARE_REV_UUID), HI_UINT16(FIRMWARE_REV_UUID)
};
static const uint8_t hardware_uuid[ATT_BT_UUID_SIZE] FLASH_CONST = {
    LO_UINT16(HARDWARE_REV_UUID), HI_UINT16(HARDWARE_REV_UUID)
};
static const uint8_t manufacturer_uuid[ATT_BT_UUID_SIZE] FLASH_CONST = {
    LO_UINT16(MANUFACTURER_NAME_UUID), HI_UINT16(MANUFACTURER_NAME_UUID)
};

static const gattAttrType_t service FLASH_CONST = {
    ATT_BT_UUID_SIZE, service_uuid
};
static uint8_t read_property = GATT_PROP_READ;
static const uint8_t model[] FLASH_CONST = "CH572 BLE UART";
static uint8_t serial[12];
static const uint8_t firmware[] FLASH_CONST = "0.2.0";
static const uint8_t hardware[] FLASH_CONST = "CH572";
static const uint8_t manufacturer[] FLASH_CONST = "WCH Serial Tool";

#define READ_CHARACTERISTIC(uuid, value) \
    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0, \
     &read_property}, \
    {{ATT_BT_UUID_SIZE, uuid}, GATT_PERMIT_READ, 0, (uint8_t *)value}

static gattAttribute_t attributes[] = {
    {{ATT_BT_UUID_SIZE, primaryServiceUUID}, GATT_PERMIT_READ, 0,
     (uint8_t *)&service},
    READ_CHARACTERISTIC(model_uuid, model),
    READ_CHARACTERISTIC(serial_uuid, serial),
    READ_CHARACTERISTIC(firmware_uuid, firmware),
    READ_CHARACTERISTIC(hardware_uuid, hardware),
    READ_CHARACTERISTIC(manufacturer_uuid, manufacturer)
};

static bStatus_t read_attribute(uint16_t conn_handle,
                                gattAttribute_t *attribute,
                                uint8_t *value, uint16_t *length,
                                uint16_t offset, uint16_t max_length,
                                uint8_t method)
{
    const uint8_t *source;
    uint16_t source_length;
    uint16_t uuid;

    (void)conn_handle;
    (void)method;
    uuid = BUILD_UINT16(attribute->type.uuid[0], attribute->type.uuid[1]);
    switch(uuid)
    {
        case MODEL_NUMBER_UUID:
            source = model;
            source_length = sizeof(model) - 1U;
            break;
        case SERIAL_NUMBER_UUID:
            source = serial;
            source_length = sizeof(serial);
            break;
        case FIRMWARE_REV_UUID:
            source = firmware;
            source_length = sizeof(firmware) - 1U;
            break;
        case HARDWARE_REV_UUID:
            source = hardware;
            source_length = sizeof(hardware) - 1U;
            break;
        case MANUFACTURER_NAME_UUID:
            source = manufacturer;
            source_length = sizeof(manufacturer) - 1U;
            break;
        default:
            return ATT_ERR_ATTR_NOT_FOUND;
    }

    if(offset >= source_length)
        return ATT_ERR_INVALID_OFFSET;
    source_length -= offset;
    if(source_length > max_length)
        source_length = max_length;
    tmos_memcpy(value, source + offset, source_length);
    *length = source_length;
    return SUCCESS;
}

static gattServiceCBs_t callbacks = {
    read_attribute,
    NULL,
    NULL
};

bStatus_t BleDeviceInfo_Add(void)
{
    static const uint8_t hex[] = "0123456789ABCDEF";
    __attribute__((aligned(4))) uint8_t mac[8];
    uint8_t index;

    GetMACAddress(mac);
    for(index = 0; index < 6U; index++)
    {
        serial[index * 2U] = hex[mac[index] >> 4];
        serial[index * 2U + 1U] = hex[mac[index] & 0x0fU];
    }

    return GATTServApp_RegisterService(attributes,
                                       GATT_NUM_ATTRS(attributes),
                                       GATT_MAX_ENCRYPT_KEY_SIZE,
                                       &callbacks);
}
