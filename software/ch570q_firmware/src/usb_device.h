#ifndef CH572_USB_DEVICE_H
#define CH572_USB_DEVICE_H

#include <stdint.h>

void UsbDevice_Init(void);
void UsbDevice_Disable(void);
void UsbDevice_Task(void);
uint8_t UsbDevice_IsConfigured(void);
uint32_t UsbDevice_GetActivityCount(void);
uint8_t UsbDevice_VendorRead(uint8_t *data, uint8_t capacity);
uint8_t UsbDevice_VendorWrite(const uint8_t *data, uint8_t length);
uint8_t UsbDevice_RfRead(uint8_t *data, uint8_t capacity);
uint8_t UsbDevice_RfWrite(const uint8_t *data, uint8_t length);
uint8_t UsbDevice_RfLineCoding(uint32_t *baud, uint8_t *stop_bits,
                               uint8_t *parity, uint8_t *data_bits,
                               uint8_t *io_status);

#endif
