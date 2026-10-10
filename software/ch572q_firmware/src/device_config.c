#include "CONFIG.h"

#include "config_protocol.h"
#include "device_config.h"

#include <stddef.h>

/* The application ends at 0x3A000; this 4 KiB block is reserved for settings. */
#define DEVICE_CONFIG_ADDRESS 0x0003A000UL
#define DEVICE_CONFIG_MAGIC   0x572B1E01UL

typedef struct
{
    uint32_t magic;
    uint8_t tx_power;
    uint8_t xtal_capacitance;
    uint8_t reserved[2];
    uint32_t crc32;
} device_config_t;

static device_config_t settings;

uint8_t DeviceConfig_XtalCapSupported(uint8_t capacitance)
{
    return capacitance >= 6U && capacitance <= 20U &&
           (capacitance & 1U) == 0U;
}

uint8_t DeviceConfig_PowerSupported(uint8_t power)
{
    switch(power)
    {
        case LL_TX_PWR_MINUS_25_DBM:
        case LL_TX_PWR_MINUS_20_DBM:
        case LL_TX_PWR_MINUS_15_DBM:
        case LL_TX_PWR_MINUS_10_DBM:
        case LL_TX_PWR_MINUS_8_DBM:
        case LL_TX_PWR_MINUS_5_DBM:
        case LL_TX_PWR_MINUS_3_DBM:
        case LL_TX_PWR_MINUS_1_DBM:
        case LL_TX_PWR_0_DBM:
        case LL_TX_PWR_1_DBM:
        case LL_TX_PWR_2_DBM:
        case LL_TX_PWR_3_DBM:
        case LL_TX_PWR_4_DBM:
        case LL_TX_PWR_5_DBM:
        case LL_TX_PWR_6_DBM:
        case LL_TX_PWR_7_DBM:
            return 1;
        default:
            return 0;
    }
}

void DeviceConfig_Init(void)
{
    const device_config_t *saved =
        (const device_config_t *)DEVICE_CONFIG_ADDRESS;

    if(saved->magic == DEVICE_CONFIG_MAGIC &&
       saved->crc32 == Config_Crc32(saved,
                                     offsetof(device_config_t, crc32)) &&
       DeviceConfig_PowerSupported(saved->tx_power))
    {
        settings = *saved;
        if(!DeviceConfig_XtalCapSupported(settings.xtal_capacitance))
            settings.xtal_capacitance = 18U;
    }
    else
    {
        settings.magic = DEVICE_CONFIG_MAGIC;
        settings.tx_power = LL_TX_PWR_0_DBM;
        settings.xtal_capacitance = 18U;
        settings.reserved[0] = 0;
        settings.reserved[1] = 0;
        settings.crc32 = 0;
    }
}

uint8_t DeviceConfig_GetTxPower(void)
{
    return settings.tx_power;
}

uint8_t DeviceConfig_GetXtalCap(void)
{
    return settings.xtal_capacitance;
}

static uint8_t save(void)
{
    settings.crc32 = Config_Crc32(&settings,
                                  offsetof(device_config_t, crc32));
    if(FLASH_ROM_ERASE(DEVICE_CONFIG_ADDRESS, FLASH_BLOCK_SIZE))
        return 1;
    return (uint8_t)FLASH_ROM_WRITE(DEVICE_CONFIG_ADDRESS,
                                    &settings, sizeof(settings));
}

uint8_t DeviceConfig_SetTxPower(uint8_t power)
{
    uint8_t previous = settings.tx_power;

    if(previous == power)
        return 0;
    settings.tx_power = power;
    if(save())
    {
        settings.tx_power = previous;
        return 1;
    }
    return 0;
}

uint8_t DeviceConfig_SetXtalCap(uint8_t capacitance)
{
    uint8_t previous = settings.xtal_capacitance;

    if(!DeviceConfig_XtalCapSupported(capacitance))
        return 1;
    if(previous == capacitance)
        return 0;
    settings.xtal_capacitance = capacitance;
    if(save())
    {
        settings.xtal_capacitance = previous;
        return 1;
    }
    return 0;
}
