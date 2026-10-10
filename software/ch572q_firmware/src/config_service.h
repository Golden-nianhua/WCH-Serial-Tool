#ifndef CH572_CONFIG_SERVICE_H
#define CH572_CONFIG_SERVICE_H

#include <stdint.h>

void ConfigService_Init(void);
void ConfigService_Task(void);
void ConfigService_ProcessPacket(const uint8_t *request, uint8_t *response);

#endif
