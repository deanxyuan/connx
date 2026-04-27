/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 *
 * Echo client using connx C API with newline-delimited protocol.
 * Usage: client_c <host:port>
 * Example:
 *  ./server 9000       (in one terminal)
 *  ./client_c 127.0.0.1:9000  (in another terminal)
 */

#include <connx/c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif
static connx_client_t* g_client = NULL;
static int g_done = 0;

static void on_connected(void* ud) {
    (void)ud;
    printf("[event] connected\n");
    const char* msg = "hello from connx (C API)\n";
    if (connx_client_send_buffer(g_client, msg, strlen(msg)) == 0) {
        printf("[send] %s", msg);
    } else {
        printf("[send] failed\n");
    }
}

static void on_connect_failed(void* ud, const char* reason) {
    (void)ud;
    printf("[event] connect failed: %s\n", reason ? reason : "unknown");
    g_done = 1;
}

static void on_closed(void* ud) {
    (void)ud;
    printf("[event] connection closed\n");
    g_done = 1;
}

static void on_message(void* ud, const void* data, size_t size) {
    (void)ud;
    printf("[recv] %.*s", (int)size, (const char*)data);
    // Echo once more, then disconnect
    static int count = 0;
    if (++count < 3) {
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "echo #%d\n", count);
        connx_client_send_buffer(g_client, buf, (size_t)len);
        printf("[send] %s", buf);
    } else {
        printf("[info] disconnecting after %d exchanges\n", count);
        connx_client_disconnect(g_client);
        g_done = 1;
    }
}

static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <host:port>\n", argv[0]);
        return 1;
    }
    const char* host = argv[1];
    // Handler
    connx_client_handler_t* handler =
        connx_client_handler_new(NULL, on_connected, on_connect_failed, on_closed, on_message);
    // Codec: newline-delimited
    connx_codec_t* codec = connx_codec_new_delimiter('\n');
    // Options
    connx_client_options_t* opts = connx_client_options_new();
    connx_client_options_set_codec(opts, codec);
    connx_client_options_set_connect_timeout(opts, 5000);
    // Client
    g_client = connx_client_new(handler, opts);
    if (!g_client) {
        fprintf(stderr, "failed to create client\n");
        goto cleanup;
    }

    printf("[info] connecting to %s ...\n", host);
    if (connx_client_connect(g_client, host) != 0) {
        fprintf(stderr, "connect failed immediately\n");
        goto cleanup;
    }
    // Wait for callbacks to complete
    while (!g_done) {
        sleep_ms(100);
    }
    // Print metrics before destroy
    connx_metrics_t m;
    connx_client_get_metrics(g_client, &m);
    printf("[metrics] sent=%llu recv=%llu\n", (unsigned long long)m.bytes_sent,
           (unsigned long long)m.bytes_received);
cleanup:
    if (g_client) connx_client_destroy(g_client);
    connx_client_options_destroy(opts);
    connx_codec_destroy(codec);
    connx_client_handler_destroy(handler);
    return 0;
}
