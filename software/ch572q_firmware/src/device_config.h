#ifndef CH572_DEVICE_CONFIG_H
#define CH572_DEVICE_CONFIG_H

#include <stdint.h>

void DeviceConfig_Init(void);
uint8_t DeviceConfig_GetTxPower(void);
uint8_t DeviceConfig_PowerSupported(uint8_t power);
uint8_t DeviceConfig_SetTxPower(uint8_t power);

#endif
