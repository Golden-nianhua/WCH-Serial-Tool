#include "config_protocol.h"

#include <string.h>

uint32_t Config_Crc32(const void *data, uint32_t length)
{
    const uint8_t *p = data;
    uint32_t crc = 0xffffffffUL;

    while(length--)
    {
        uint8_t bit;
        crc ^= *p++;
        for(bit = 0; bit < 8U; bit++)
            crc = (crc >> 1) ^ (0xedb88320UL & (0U - (crc & 1U)));
    }
    return ~crc;
}

uint8_t Config_PacketValid(const config_packet_t *packet)
{
    return packet->magic[0] == CFG_MAGIC0 &&
           packet->magic[1] == CFG_MAGIC1 &&
           packet->version == CFG_PROTOCOL_VERSION &&
           packet->length <= CFG_PAYLOAD_SIZE &&
           packet->crc32 == Config_Crc32(packet, CFG_PACKET_SIZE - 4U);
}

void Config_MakeResponse(config_packet_t *response,
                         const config_packet_t *request,
                         uint8_t status, const void *payload,
                         uint16_t length)
{
    uint8_t command = request->command;
    uint16_t sequence = request->sequence;

    if(length > CFG_PAYLOAD_SIZE)
        length = CFG_PAYLOAD_SIZE;
    if(length && payload != response->payload)
        memmove(response->payload, payload, length);
    if(length < CFG_PAYLOAD_SIZE)
        memset(response->payload + length, 0, CFG_PAYLOAD_SIZE - length);
    response->magic[0] = CFG_MAGIC0;
    response->magic[1] = CFG_MAGIC1;
    response->version = CFG_PROTOCOL_VERSION;
    response->command = command | 0x80U;
    response->sequence = sequence;
    response->argument = status;
    response->length = length;
    response->crc32 = Config_Crc32(response, CFG_PACKET_SIZE - 4U);
}
