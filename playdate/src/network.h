#pragma once

#include "pd_api.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    JD_NET_IDLE,
    JD_NET_REQUESTING_ACCESS,
    JD_NET_CONNECTING_WIFI,
    JD_NET_CONNECTING_STREAM,
    JD_NET_CONNECTED,
    JD_NET_FAILED,
    JD_NET_CLOSED
} JDNetworkState;

typedef void (*JDNetworkBytesCallback)(void* context, const uint8_t* bytes, size_t length);

typedef struct {
    PlaydateAPI* pd;
    TCPConnection* connection;
    JDNetworkState state;
    JDNetworkBytesCallback bytes_callback;
    void* callback_context;
    uint8_t outgoing[1024];
    size_t outgoing_used;
    size_t outgoing_sent;
    char error[96];
} JDNetwork;

void jd_network_init(
    JDNetwork* network,
    PlaydateAPI* playdate,
    JDNetworkBytesCallback callback,
    void* context
);
void jd_network_connect(JDNetwork* network, const char* host, int port);
void jd_network_flush(JDNetwork* network);
void jd_network_poll(JDNetwork* network);
int jd_network_send(JDNetwork* network, const uint8_t* bytes, size_t length);
void jd_network_close(JDNetwork* network);
const char* jd_network_state_text(JDNetworkState state);
