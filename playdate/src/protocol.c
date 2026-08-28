#include "protocol.h"

#include <string.h>

static void write_u16(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_u32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void write_u64(uint8_t* bytes, uint64_t value) {
    write_u32(bytes, (uint32_t)(value >> 32));
    write_u32(bytes + 4, (uint32_t)value);
}

uint16_t jd_protocol_read_u16(const uint8_t* bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

uint32_t jd_protocol_read_u32(const uint8_t* bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

uint64_t jd_protocol_read_u64(const uint8_t* bytes) {
    return ((uint64_t)jd_protocol_read_u32(bytes) << 32) |
           jd_protocol_read_u32(bytes + 4);
}

void jd_protocol_parser_init(
    JDProtocolParser* parser,
    JDProtocolPacketCallback callback,
    void* context
) {
    memset(parser, 0, sizeof(*parser));
    parser->callback = callback;
    parser->callback_context = context;
}

static int decode_header(JDProtocolParser* parser) {
    const uint8_t* bytes = parser->header_bytes;
    if (memcmp(bytes, "JDAT", 4) != 0 || bytes[4] != JD_PROTOCOL_VERSION) return 0;
    parser->header.type = bytes[5];
    parser->header.flags = jd_protocol_read_u16(bytes + 6);
    parser->header.payload_length = jd_protocol_read_u32(bytes + 8);
    parser->header.timestamp_us = jd_protocol_read_u64(bytes + 12);
    parser->header.sequence = jd_protocol_read_u32(bytes + 20);
    return parser->header.payload_length <= JD_MAX_PAYLOAD_SIZE;
}

int jd_protocol_parser_push(JDProtocolParser* parser, const uint8_t* bytes, size_t length) {
    size_t cursor = 0;
    if (parser->failed) return 0;

    while (cursor < length) {
        if (parser->header_used < JD_HEADER_SIZE) {
            size_t needed = JD_HEADER_SIZE - parser->header_used;
            size_t available = length - cursor;
            size_t copy = needed < available ? needed : available;
            memcpy(parser->header_bytes + parser->header_used, bytes + cursor, copy);
            parser->header_used += copy;
            cursor += copy;
            if (parser->header_used < JD_HEADER_SIZE) continue;
            if (!decode_header(parser)) {
                parser->failed = 1;
                return 0;
            }
        }

        if (parser->payload_used < parser->header.payload_length) {
            size_t needed = parser->header.payload_length - parser->payload_used;
            size_t available = length - cursor;
            size_t copy = needed < available ? needed : available;
            memcpy(parser->payload + parser->payload_used, bytes + cursor, copy);
            parser->payload_used += copy;
            cursor += copy;
            if (parser->payload_used < parser->header.payload_length) continue;
        }

        parser->callback(parser->callback_context, &parser->header, parser->payload);
        parser->header_used = 0;
        parser->payload_used = 0;
    }
    return 1;
}

size_t jd_protocol_encode_packet(
    uint8_t* output,
    size_t capacity,
    uint8_t type,
    uint16_t flags,
    uint64_t timestamp_us,
    uint32_t sequence,
    const uint8_t* payload,
    uint32_t payload_length
) {
    size_t total = JD_HEADER_SIZE + payload_length;
    if (capacity < total) return 0;
    memcpy(output, "JDAT", 4);
    output[4] = JD_PROTOCOL_VERSION;
    output[5] = type;
    write_u16(output + 6, flags);
    write_u32(output + 8, payload_length);
    write_u64(output + 12, timestamp_us);
    write_u32(output + 20, sequence);
    if (payload_length > 0 && payload != NULL) memcpy(output + JD_HEADER_SIZE, payload, payload_length);
    return total;
}

size_t jd_protocol_encode_play(uint8_t* output, size_t capacity, const char* item_id, uint64_t start_ms) {
    uint8_t payload[264];
    size_t id_length = strlen(item_id);
    if (id_length > 255) return 0;
    payload[0] = (uint8_t)id_length;
    memcpy(payload + 1, item_id, id_length);
    write_u64(payload + 1 + id_length, start_ms);
    return jd_protocol_encode_packet(
        output, capacity, JD_PACKET_PLAY, 0, 0, 1,
        payload, (uint32_t)(1 + id_length + 8)
    );
}

