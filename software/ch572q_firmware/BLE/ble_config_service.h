#ifndef CH572_BLE_CONFIG_SERVICE_H
#define CH572_BLE_CONFIG_SERVICE_H

#include "CONFIG.h"

typedef void (*ble_config_callback_t)(const uint8_t *data, uint16_t length);

bStatus_t BleConfigService_Add(ble_config_callback_t callback);
uint8_t BleConfigService_NotifyReady(uint16_t conn_handle);
bStatus_t BleConfigService_Notify(uint16_t conn_handle,
                                  attHandleValueNoti_t *notification);

#endif
