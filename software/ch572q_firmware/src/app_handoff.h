#ifndef CH572_APP_HANDOFF_H
#define CH572_APP_HANDOFF_H

#include <stdint.h>

#define BLE_APP_START 0x00012000UL
#define BLE_APP_MAGIC 0x424C4532UL

typedef struct
{
    uint32_t magic;
    uint8_t mac[6];
    uint8_t reserved[2];
    uint32_t check;
} app_handoff_t;

extern volatile app_handoff_t app_handoff;

#endif
