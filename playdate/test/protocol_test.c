#include "protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int callback_count;
static JDProtocolHeader received_header;
static uint8_t received_payload[16];

static void receive_packet(void* context, const JDProtocolHeader* header, const uint8_t* payload) {
    (void)context;
    callback_count += 1;
    received_header = *header;
    memcpy(received_payload, payload, header->payload_length);
}

int main(void) {
    JDProtocolParser parser;
    uint8_t wire[64];
    const uint8_t payload[] = { 1, 2, 3, 4 };
    size_t length = jd_protocol_encode_packet(
        wire, sizeof(wire), JD_PACKET_VIDEO_KEYFRAME, 1,
        1234567, 42, payload, sizeof(payload)
    );
    assert(length == JD_HEADER_SIZE + sizeof(payload));
    jd_protocol_parser_init(&parser, receive_packet, NULL);
    assert(jd_protocol_parser_push(&parser, wire, 3));
    assert(jd_protocol_parser_push(&parser, wire + 3, 14));
    assert(jd_protocol_parser_push(&parser, wire + 17, length - 17));
    assert(callback_count == 1);
    assert(received_header.type == JD_PACKET_VIDEO_KEYFRAME);
    assert(received_header.timestamp_us == 1234567);
    assert(received_header.sequence == 42);
    assert(memcmp(received_payload, payload, sizeof(payload)) == 0);
    puts("protocol_test: ok");
    return 0;
}

