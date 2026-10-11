#ifndef CH32V208_USB_DEVICE_H
#define CH32V208_USB_DEVICE_H

#include <stdint.h>

void UsbDevice_Init(void);
void UsbDevice_Disable(void);
void UsbDevice_Task(void);
uint8_t UsbDevice_IsConfigured(void);
uint32_t UsbDevice_GetActivityCount(void);
uint8_t UsbDevice_VendorRead(uint8_t *data, uint8_t capacity);
uint8_t UsbDevice_VendorWrite(const uint8_t *data, uint8_t length);

#endif
