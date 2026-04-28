/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "connx/c.h"

#include <stdlib.h>
#include <string.h>

#include "connx/client.h"
#include "connx/codec.h"
#include "connx/codec/delimiter_codec.h"
#include "connx/codec/fixed_length_codec.h"
#include "connx/codec/length_field_codec.h"
#include "connx/options.h"
#include "src/utils/string.h"

using namespace connx;

// ============================================================================
// C-compatible Codec wrapper (for callback-based custom codecs)
// ============================================================================

struct connx_codec_s {
    Codec* codec_;
};

struct connx_decode_callback_s : public Codec {
    connx_decode_callback_t callback_;
    void* userdata_;

    connx_decode_callback_s(connx_decode_callback_t cb, void* ud)
        : callback_(cb)
        , userdata_(ud) {}

    DecodeResult Decode(const char* data, size_t len, size_t* consumed_len) override {
        if (!callback_) return DecodeResult::kError;
        return static_cast<DecodeResult>(callback_(userdata_, data, len, consumed_len));
    }
};

// ============================================================================
// C-compatible ClientHandler wrapper
// ============================================================================

struct connx_client_handler_s : public ClientHandler {
    void* userdata_;
    void (*on_connected_)(void*);
    void (*on_connect_failed_)(void*, const char* reason);
    void (*on_closed_)(void*);
    void (*on_message_)(void*, const void* data, size_t size);

    void OnConnected() override {
        if (on_connected_) on_connected_(userdata_);
    }
    void OnConnectFailed(const char* reason) override {
        if (on_connect_failed_) on_connect_failed_(userdata_, reason);
    }
    void OnClosed() override {
        if (on_closed_) on_closed_(userdata_);
    }
    void OnMessage(const void* data, size_t len) override {
        if (on_message_) on_message_(userdata_, data, len);
    }
};

// ============================================================================
// C-compatible ClientOptions
// ============================================================================

struct connx_client_options_s {
    ClientOptions opts;
    char* local_address_copy; // owned copy for local_address lifetime
};

// ============================================================================
// C-compatible Client
// ============================================================================

struct connx_client_s {
    Client* client;
    ClientHandler* handler;
};

// ============================================================================
// Handler
// ============================================================================
connx_client_handler_t*
connx_client_handler_new(void* userdata, void (*on_connected)(void*),
                         void (*on_connect_failed)(void*, const char* reason),
                         void (*on_closed)(void*),
                         void (*on_message)(void*, const void* data, size_t size)) {

    auto h = new connx_client_handler_s;
    h->userdata_ = userdata;
    h->on_connected_ = on_connected;
    h->on_connect_failed_ = on_connect_failed;
    h->on_closed_ = on_closed;
    h->on_message_ = on_message;
    return h;
}

void connx_client_handler_destroy(connx_client_handler_t* handler) {
    if (handler) delete handler;
}

// ============================================================================
// Codec
// ============================================================================
connx_codec_t* connx_codec_new_delimiter(char delimiter) {
    auto wrapper = new connx_codec_s;
    wrapper->codec_ = new DelimiterCodec(delimiter);
    return wrapper;
}
connx_codec_t* connx_codec_new_fixed_length(size_t frame_length) {
    auto wrapper = new connx_codec_s;
    wrapper->codec_ = new FixedLengthCodec(frame_length);
    return wrapper;
}
connx_codec_t* connx_codec_new_length_field(uint32_t length_field_offset, uint32_t length_field_length,
                                            uint32_t header_len, uint32_t network_to_host) {
    auto wrapper = new connx_codec_s;
    wrapper->codec_ = new LengthFieldCodec(length_field_offset, length_field_length, header_len,
                                           network_to_host != 0);
    return wrapper;
}
connx_codec_t* connx_codec_new_callback(connx_decode_callback_t callback, void* userdata) {
    if (!callback) return nullptr;
    auto wrapper = new connx_codec_s;
    wrapper->codec_ = new connx_decode_callback_s(callback, userdata);
    return wrapper;
}

void connx_codec_destroy(connx_codec_t* codec) {
    if (codec) {
        delete codec->codec_;
        delete codec;
    }
}

// ============================================================================
// ClientOptions
// ============================================================================

connx_client_options_t* connx_client_options_new() {
    auto opts = new connx_client_options_s;
    opts->local_address_copy = nullptr;
    return opts;
}

void connx_client_options_destroy(connx_client_options_t* options) {
    if (options) {
        if (options->local_address_copy) {
            free(options->local_address_copy);
        }
        delete options;
    }
}
void connx_client_options_set_codec(connx_client_options_t* options, connx_codec_t* codec) {
    if (!options || !codec) return;
    options->opts.codec = codec->codec_;
}
void connx_client_options_set_local_address(connx_client_options_t* options,
                                            const char* local_address) {
    if (!options) return;
    if (options->local_address_copy) {
        free(options->local_address_copy);
    }
    if (local_address) {
        options->local_address_copy = connx_strdup(local_address);
        options->opts.local_address = options->local_address_copy;
    } else {
        options->local_address_copy = nullptr;
        options->opts.local_address = nullptr;
    }
}
void connx_client_options_set_tcp_nodelay(connx_client_options_t* options, int enabled) {
    if (!options) return;
    options->opts.tcp.tcp_nodelay = enabled ? 1 : 0;
}
void connx_client_options_set_send_buffer_size(connx_client_options_t* options, int size) {
    if (!options) return;
    options->opts.tcp.send_buffer_size = size;
}
void connx_client_options_set_recv_buffer_size(connx_client_options_t* options, int size) {
    if (!options) return;
    options->opts.tcp.recv_buffer_size = size;
}
void connx_client_options_set_linger(connx_client_options_t* options, int sec) {
    if (!options) return;
    options->opts.tcp.linger_sec = sec;
}
void connx_client_options_set_connect_timeout(connx_client_options_t* options, int ms) {
    if (!options) return;
    options->opts.tcp.connect_timeout_ms = ms;
}

// ============================================================================
// Client Lifecycle
// ============================================================================
connx_client_t* connx_client_new(connx_client_handler_t* handler, connx_client_options_t* opts) {
    if (!handler || !opts) return nullptr;

    auto c = new connx_client_s;
    c->handler = handler;
    c->client = CreateClient(handler, opts->opts);
    if (!c->client) {
        delete c;
        return nullptr;
    }
    return c;
}
void connx_client_destroy(connx_client_t* client) {
    if (client) {
        if (client->client) {
            ReleaseClient(client->client);
        }
        delete client;
    }
}

// ============================================================================
// Connection Management
// ============================================================================
int connx_client_connect(connx_client_t* client, const char* host) {
    if (!client || !client->client || !host) return -1;
    return client->client->Connect(host) ? 0 : -1;
}
void connx_client_disconnect(connx_client_t* client) {
    if (!client || !client->client) return;
    client->client->Disconnect();
}
int connx_client_is_connected(const connx_client_t* client) {
    if (!client || !client->client) return 0;
    return client->client->IsConnected() ? 1 : 0;
}

// ============================================================================
// Data Transmission
// ============================================================================

int connx_client_send_buffer(connx_client_t* client, const void* data, size_t len) {
    if (!client || !client->client || !data || len == 0) return -1;
    return client->client->SendBuffer(data, len) ? 0 : -1;
}

// ============================================================================
// Observability
// ============================================================================
void connx_client_get_metrics(const connx_client_t* client, connx_metrics_t* metrics) {
    if (!client || !client->client || !metrics) return;

    Metrics m;
    client->client->GetMetrics(&m);
    metrics->state = static_cast<int>(m.state);
    metrics->bytes_sent = m.bytes_sent;
    metrics->bytes_received = m.bytes_received;
    metrics->pending_bytes = m.pending_bytes;
}
// ============================================================================
// Library Lifecycle
// ============================================================================
void connx_library_init() { connx::LibraryInit(); }
void connx_library_shutdown() { connx::LibraryShutdown(); }
// ============================================================================
// Version
// ============================================================================
#ifndef CONNX_VERSION
#    define CONNX_VERSION "1.0.0"
#endif
#ifndef CONNX_VERSION_MAJOR
#    define CONNX_VERSION_MAJOR 1
#endif
#ifndef CONNX_VERSION_MINOR
#    define CONNX_VERSION_MINOR 0
#endif
#ifndef CONNX_VERSION_PATCH
#    define CONNX_VERSION_PATCH 0
#endif
const char* connx_version_string(void) { return CONNX_VERSION; }
int connx_version_major(void) { return CONNX_VERSION_MAJOR; }
int connx_version_minor(void) { return CONNX_VERSION_MINOR; }
int connx_version_patch(void) { return CONNX_VERSION_PATCH; }
