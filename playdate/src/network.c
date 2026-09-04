#include "network.h"

#include <stdio.h>
#include <string.h>

#define JD_NETWORK_READ_CHUNK 4096

static char target_host[128];
static int target_port;
static PlaydateAPI* pd;
static JDNetwork* active_network;

static void set_error(JDNetwork* network, const char* label, int code) {
    snprintf(network->error, sizeof(network->error), "%s (%d)", label, code);
    network->state = JD_NET_FAILED;
}

static void connection_closed(TCPConnection* connection, PDNetErr error) {
    JDNetwork* network = (JDNetwork*)pd->network->tcp->getUserdata(connection);
    if (network->state != JD_NET_CLOSED) {
        set_error(network, "signal lost", error);
    }
}

static void connection_opened(TCPConnection* connection, PDNetErr error, void* userdata) {
    JDNetwork* network = (JDNetwork*)userdata;
    if (error != NET_OK) {
        set_error(network, "stream connection failed", error);
        return;
    }
    /* Hardware initializes parts of the TCP connection during open(), so set
       the read timeout here rather than before open(). This keeps a stale
       getBytesAvailable() result from turning a tiny read into a watchdog
       stall. */
    network->pd->network->tcp->setReadTimeout(connection, 2);
    network->state = JD_NET_CONNECTED;
}

static void access_decided(bool allowed, void* userdata) {
    JDNetwork* network = (JDNetwork*)userdata;
    PDNetErr result;
    if (!allowed) {
        set_error(network, "network access denied", NET_CONNECTION_CLOSED);
        return;
    }

    network->state = JD_NET_CONNECTING_STREAM;
    network->connection = network->pd->network->tcp->newConnection(target_host, target_port, false);
    if (network->connection == NULL) {
        set_error(network, "could not create stream socket", NET_CONNECTION_CLOSED);
        return;
    }
    network->pd->network->tcp->setUserdata(network->connection, network);
    network->pd->network->tcp->setConnectTimeout(network->connection, 10000);
    network->pd->network->tcp->setConnectionClosedCallback(network->connection, connection_closed);
    result = network->pd->network->tcp->open(network->connection, connection_opened, network);
    if (result != NET_OK) set_error(network, "could not open stream socket", result);
}

static void wifi_enabled(PDNetErr error) {
    if (error != NET_OK && active_network != NULL) {
        set_error(active_network, "wifi connection failed", error);
    }
}

void jd_network_init(
    JDNetwork* network,
    PlaydateAPI* playdate,
    JDNetworkBytesCallback callback,
    void* context
) {
    memset(network, 0, sizeof(*network));
    pd = playdate;
    active_network = network;
    network->pd = playdate;
    network->state = JD_NET_IDLE;
    network->bytes_callback = callback;
    network->callback_context = context;
}

void jd_network_connect(JDNetwork* network, const char* host, int port) {
    size_t host_length = strlen(host);
    enum accessReply access;
    if (host_length >= sizeof(target_host)) {
        set_error(network, "bridge host is too long", NET_CONNECTION_CLOSED);
        return;
    }
    memcpy(target_host, host, host_length + 1);
    target_port = port;
    network->state = JD_NET_CONNECTING_WIFI;
    network->pd->network->setEnabled(true, wifi_enabled);
    network->state = JD_NET_REQUESTING_ACCESS;
    access = network->pd->network->tcp->requestAccess(
        target_host,
        target_port,
        false,
        "Tune in to the Jellydate Bridge",
        access_decided,
        network
    );
    if (access == kAccessAllow) access_decided(true, network);
    else if (access == kAccessDeny) access_decided(false, network);
}

int jd_network_send(JDNetwork* network, const uint8_t* bytes, size_t length) {
    if (length > sizeof(network->outgoing) - network->outgoing_used) return 0;
    memcpy(network->outgoing + network->outgoing_used, bytes, length);
    network->outgoing_used += length;
    return 1;
}

void jd_network_flush(JDNetwork* network) {
    int written;
    size_t remaining;
    if (network->state != JD_NET_CONNECTED || network->outgoing_sent >= network->outgoing_used) return;
    remaining = network->outgoing_used - network->outgoing_sent;
    written = (int)network->pd->network->tcp->write(
        network->connection,
        network->outgoing + network->outgoing_sent,
        remaining
    );
    if (written < 0) {
        if (written != NET_WRITE_BUSY) set_error(network, "stream write failed", written);
        return;
    }
    network->outgoing_sent += (size_t)written;
    if (network->outgoing_sent == network->outgoing_used) {
        network->outgoing_sent = 0;
        network->outgoing_used = 0;
    }
}

void jd_network_poll(JDNetwork* network) {
    uint8_t incoming[JD_NETWORK_READ_CHUNK];
    size_t available;
    size_t wanted;
    int read;
    if (network->state != JD_NET_CONNECTED || network->connection == NULL) return;
    jd_network_flush(network);
    available = network->pd->network->tcp->getBytesAvailable(network->connection);
    if (available == 0) return;

    /* Never ask the firmware to wait for bytes that were not in its
       availability snapshot. A seek can stop the old stream immediately after
       this check, and repeated timeout-backed reads at that boundary have
       triggered the hardware watchdog. One exact, capped read per update keeps
       the call non-blocking while the protocol parser joins partial packets. */
    wanted = available < sizeof(incoming) ? available : sizeof(incoming);
    read = network->pd->network->tcp->read(network->connection, incoming, wanted);
    if (read < 0) {
        if (read != NET_READ_BUSY && read != NET_READ_TIMEOUT) {
            set_error(network, "stream read failed", read);
        }
        return;
    }
    if (read == 0) return;
    network->bytes_callback(network->callback_context, incoming, (size_t)read);
}

void jd_network_close(JDNetwork* network) {
    network->state = JD_NET_CLOSED;
    if (network->connection != NULL) {
        network->pd->network->tcp->close(network->connection);
        network->pd->network->tcp->release(network->connection);
        network->connection = NULL;
    }
    /* Bytes queued for a dead TCP stream cannot be valid on its successor.
       In particular, retaining a partial command can crowd out the AUTH/PLAY
       pair required to recover playback. */
    network->outgoing_used = 0;
    network->outgoing_sent = 0;
}

const char* jd_network_state_text(JDNetworkState state) {
    switch (state) {
        case JD_NET_IDLE: return "idle";
        case JD_NET_REQUESTING_ACCESS: return "asking permission";
        case JD_NET_CONNECTING_WIFI: return "finding signal";
        case JD_NET_CONNECTING_STREAM: return "tuning bridge";
        case JD_NET_CONNECTED: return "connected";
        case JD_NET_FAILED: return "signal lost";
        case JD_NET_CLOSED: return "off air";
    }
    return "unknown";
}
