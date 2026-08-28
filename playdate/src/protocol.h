#pragma once

#include <stddef.h>
#include <stdint.h>

#define JD_HEADER_SIZE 24
#define JD_MAX_PAYLOAD_SIZE 12000
#define JD_FRAME_SIZE 12000
#define JD_PROTOCOL_VERSION 1
#define JD_PACKET_FLAG_DISCONTINUITY 1

typedef enum {
    JD_PACKET_HELLO = 0x01,
    JD_PACKET_AUTH = 0x02,
    JD_PACKET_PLAY = 0x03,
    JD_PACKET_PAUSE = 0x04,
    JD_PACKET_RESUME = 0x05,
    JD_PACKET_SEEK = 0x06,
    JD_PACKET_STOP = 0x07,
    JD_PACKET_VIDEO_KEYFRAME = 0x10,
    JD_PACKET_VIDEO_DELTA = 0x11,
    JD_PACKET_AUDIO = 0x12,
    JD_PACKET_PLAYBACK_STATE = 0x13,
    JD_PACKET_BUFFER_STATE = 0x14,
    JD_PACKET_END_OF_STREAM = 0x15,
    JD_PACKET_ERROR = 0x16,
    JD_PACKET_PING = 0x17,
    JD_PACKET_PONG = 0x18,
    JD_PACKET_STREAM_INFO = 0x19,
    JD_PACKET_CLIENT_STATS = 0x1A,
    JD_PACKET_HOME_REQUEST = 0x20,
    JD_PACKET_HOME_RESPONSE = 0x21
} JDProtocolPacketType;

typedef struct {
    uint8_t type;
    uint16_t flags;
    uint32_t payload_length;
    uint64_t timestamp_us;
    uint32_t sequence;
} JDProtocolHeader;

typedef void (*JDProtocolPacketCallback)(
    void* context,
    const JDProtocolHeader* header,
    const uint8_t* payload
);

typedef struct {
    uint8_t header_bytes[JD_HEADER_SIZE];
    size_t header_used;
    JDProtocolHeader header;
    uint8_t payload[JD_MAX_PAYLOAD_SIZE];
    size_t payload_used;
    JDProtocolPacketCallback callback;
    void* callback_context;
    int failed;
} JDProtocolParser;

void jd_protocol_parser_init(
    JDProtocolParser* parser,
    JDProtocolPacketCallback callback,
    void* context
);
int jd_protocol_parser_push(JDProtocolParser* parser, const uint8_t* bytes, size_t length);

size_t jd_protocol_encode_packet(
    uint8_t* output,
    size_t capacity,
    uint8_t type,
    uint16_t flags,
    uint64_t timestamp_us,
    uint32_t sequence,
    const uint8_t* payload,
    uint32_t payload_length
);
size_t jd_protocol_encode_play(uint8_t* output, size_t capacity, const char* item_id, uint64_t start_ms);
uint16_t jd_protocol_read_u16(const uint8_t* bytes);
uint32_t jd_protocol_read_u32(const uint8_t* bytes);
uint64_t jd_protocol_read_u64(const uint8_t* bytes);
