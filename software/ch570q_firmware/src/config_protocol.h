#ifndef CH572_CONFIG_PROTOCOL_H
#define CH572_CONFIG_PROTOCOL_H

#include <stdint.h>

#define CFG_PACKET_SIZE       64
#define CFG_PAYLOAD_SIZE      48
#define CFG_MAGIC0            0x55
#define CFG_MAGIC1            0x57
#define CFG_PROTOCOL_VERSION  1
#define CFG_CMD_GET_INFO      0x01
#define CFG_CMD_GET_STATUS    0x02
#define CFG_CMD_GET_PAIR      0x03
#define CFG_CMD_START_PAIR    0x04
#define CFG_CMD_CLEAR_PAIR    0x05
#define CFG_CMD_GET_RF_CONFIG 0x06
#define CFG_CMD_SET_RF_CONFIG 0x07
#define CFG_CMD_START_SCAN    0x08
#define CFG_CMD_GET_SCAN      0x09
#define CFG_CMD_PAIR_DEVICE   0x0a
#define CFG_CMD_RESET         0x11
#define CFG_CMD_ENTER_ISP     0x12
#define CFG_CMD_REMOTE_GET_INFO   0x21
#define CFG_CMD_REMOTE_GET_STATUS 0x22
#define CFG_CMD_REMOTE_GET_CONFIG 0x23
#define CFG_CMD_REMOTE_SET_CONFIG 0x24
#define CFG_CMD_REMOTE_RESET      0x25
#define CFG_STATUS_OK          0
#define CFG_STATUS_BAD_PACKET  1
#define CFG_STATUS_BAD_COMMAND 2
#define CFG_STATUS_BAD_LENGTH  3
#define CFG_STATUS_FLASH       5
#define CFG_STATUS_STATE       7
#define CFG_STATUS_PENDING     8
#define CFG_STATUS_TIMEOUT     9
#define IAP_APP_START 0x00002000UL
#define IAP_APP_END   0x0003A000UL

typedef struct __attribute__((packed))
{
    uint8_t magic[2];
    uint8_t version;
    uint8_t command;
    uint16_t sequence;
    uint16_t length;
    uint32_t argument;
    uint8_t payload[CFG_PAYLOAD_SIZE];
    uint32_t crc32;
} config_packet_t;

typedef char config_packet_must_be_64_bytes[
    sizeof(config_packet_t) == CFG_PACKET_SIZE ? 1 : -1
];

uint32_t Config_Crc32(const void *data, uint32_t length);
uint8_t Config_PacketValid(const config_packet_t *packet);
void Config_MakeResponse(config_packet_t *response,
                         const config_packet_t *request,
                         uint8_t status, const void *payload,
                         uint16_t length);

#endif
