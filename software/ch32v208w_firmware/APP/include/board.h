#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Board_Init(void);
void Board_PAControlInit(void);
void Board_LedTask(uint8_t connected, uint32_t usb_activity);

#ifdef __cplusplus
}
#endif

#endif
