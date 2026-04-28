/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#include "test/util/testutil.h"
#include "connx/c.h"

#include <string.h>
#include <stdio.h>

RUN_ALL_TESTS();

// VERSION API
TEST(CApiTest, version_string_not_null) {
    const char* v = connx_version_string();
    ASSERT_TRUE(v != nullptr);
    ASSERT_TRUE(strlen(v) > 0);
}

TEST(CApiTest, version_components_positive) {
    ASSERT_TRUE(connx_version_major() >= 0);
    ASSERT_TRUE(connx_version_minor() >= 0);
    ASSERT_TRUE(connx_version_patch() >= 0);
}

// CODEC CREATION AND DESTRUCTION
TEST(CApiTest, codec_delimiter_create_destroy) {
    connx_codec_t* c = connx_codec_new_delimiter('n');
    ASSERT_TRUE(c != nullptr);
    connx_codec_destroy(c);
}

TEST(CApiTest, codec_fixed_length_create_destroy) {
    connx_codec_t* c = connx_codec_new_fixed_length(128);
    ASSERT_TRUE(c != nullptr);
    connx_codec_destroy(c);
}

TEST(CApiTest, codec_length_field_create_destroy) {
    connx_codec_t* c = connx_codec_new_length_field(0, 4, 4, 1);
    ASSERT_TRUE(c != nullptr);
    connx_codec_destroy(c);
}

TEST(CApiTest, codec_callback_create_destroy) {
    auto decode_fn = [](void*, const void*, size_t, size_t*) -> connx_decode_result_t {
        return CONNX_DECODE_NEED_MORE;
    };
    connx_codec_t* c = connx_codec_new_callback(decode_fn, nullptr);
    ASSERT_TRUE(c != nullptr);
    connx_codec_destroy(c);
}

TEST(CApiTest, codec_callback_null_returns_null) {
    connx_codec_t* c = connx_codec_new_callback(nullptr, nullptr);
    ASSERT_TRUE(c == nullptr);
}
TEST(CApiTest, codec_destroy_null_safe) { connx_codec_destroy(nullptr); }

// HANDLER CREATION AND DESTRUCTION

TEST(CApiTest, handler_create_destroy) {
    connx_client_handler_t* h =
        connx_client_handler_new(nullptr, nullptr, nullptr, nullptr, nullptr);

    ASSERT_TRUE(h != nullptr);
    connx_client_handler_destroy(h);
}

TEST(CApiTest, handler_destroy_null_safe) { connx_client_handler_destroy(nullptr); }

// OPTIONS

TEST(CApiTest, options_create_destroy) {
    connx_client_options_t* opts = connx_client_options_new();
    ASSERT_TRUE(opts != nullptr);
    connx_client_options_destroy(opts);
}
TEST(CApiTest, options_set_codec) {
    connx_client_options_t* opts = connx_client_options_new();
    connx_codec_t* codec = connx_codec_new_delimiter('\n');
    connx_client_options_set_codec(opts, codec);
    connx_client_options_destroy(opts);
    connx_codec_destroy(codec);
}
TEST(CApiTest, options_set_local_address) {
    connx_client_options_t* opts = connx_client_options_new();
    connx_client_options_set_local_address(opts, "127.0.0.1");
    connx_client_options_set_local_address(opts, nullptr); // clear
    connx_client_options_destroy(opts);
}
TEST(CApiTest, options_set_tcp_options) {
    connx_client_options_t* opts = connx_client_options_new();
    connx_client_options_set_tcp_nodelay(opts, 1);
    connx_client_options_set_send_buffer_size(opts, 65536);
    connx_client_options_set_recv_buffer_size(opts, 65536);
    connx_client_options_set_linger(opts, 5);
    connx_client_options_set_connect_timeout(opts, 3000);
    connx_client_options_destroy(opts);
}
TEST(CApiTest, options_setters_null_safe) {
    connx_client_options_set_codec(nullptr, nullptr);
    connx_client_options_set_local_address(nullptr, nullptr);
    connx_client_options_set_tcp_nodelay(nullptr, 1);
    connx_client_options_set_send_buffer_size(nullptr, 0);
    connx_client_options_set_recv_buffer_size(nullptr, 0);
    connx_client_options_set_linger(nullptr, 0);
    connx_client_options_set_connect_timeout(nullptr, 0);
    connx_client_options_destroy(nullptr);
}

// CLIENT LIFECYCLE

TEST(CApiTest, client_new_destroy) {
    connx_client_handler_t* h =
        connx_client_handler_new(nullptr, nullptr, nullptr, nullptr, nullptr);
    connx_client_options_t* opts = connx_client_options_new();
    connx_codec_t* codec = connx_codec_new_delimiter('\n');
    connx_client_options_set_codec(opts, codec);
    connx_client_t* cli = connx_client_new(h, opts);
    ASSERT_TRUE(cli != nullptr);
    connx_client_destroy(cli);
    connx_client_options_destroy(opts);
    connx_codec_destroy(codec);
    connx_client_handler_destroy(h);
}

TEST(CApiTest, client_new_null_handler_returns_null) {
    connx_client_options_t* opts = connx_client_options_new();
    connx_client_t* cli = connx_client_new(nullptr, opts);
    ASSERT_TRUE(cli == nullptr);
    connx_client_options_destroy(opts);
}

TEST(CApiTest, client_new_null_options_returns_null) {
    connx_client_handler_t* h =
        connx_client_handler_new(nullptr, nullptr, nullptr, nullptr, nullptr);
    connx_client_t* cli = connx_client_new(h, nullptr);
    ASSERT_TRUE(cli == nullptr);
    connx_client_handler_destroy(h);
}

TEST(CApiTest, client_destroy_null_safe) { connx_client_destroy(nullptr); }

TEST(CApiTest, client_is_connected_before_connect) {
    connx_client_handler_t* h =
        connx_client_handler_new(nullptr, nullptr, nullptr, nullptr, nullptr);
    connx_client_options_t* opts = connx_client_options_new();
    connx_codec_t* codec = connx_codec_new_delimiter('n');
    connx_client_options_set_codec(opts, codec);
    connx_client_t* cli = connx_client_new(h, opts);
    ASSERT_EQ(connx_client_is_connected(cli), 0);
    connx_client_destroy(cli);
    connx_client_options_destroy(opts);
    connx_codec_destroy(codec);
    connx_client_handler_destroy(h);
}

// OBSERVABILITY

TEST(CApiTest, client_get_metrics) {
    connx_client_handler_t* h =
        connx_client_handler_new(nullptr, nullptr, nullptr, nullptr, nullptr);
    connx_client_options_t* opts = connx_client_options_new();
    connx_codec_t* codec = connx_codec_new_delimiter('n');
    connx_client_options_set_codec(opts, codec);
    connx_client_t* cli = connx_client_new(h, opts);
    connx_metrics_t m;
    memset(&m, 0xff, sizeof(m));
    connx_client_get_metrics(cli, &m);
    ASSERT_EQ(m.state, CONNX_STATE_DISCONNECTED);
    ASSERT_EQ(m.bytes_sent, (uint64_t)0);
    ASSERT_EQ(m.bytes_received, (uint64_t)0);
    ASSERT_EQ(m.pending_bytes, (size_t)0);
    connx_client_destroy(cli);
    connx_client_options_destroy(opts);
    connx_codec_destroy(codec);
    connx_client_handler_destroy(h);
}

// DECODE RESULT
TEST(CApiTest, decode_result_values) {
    ASSERT_EQ(CONNX_DECODE_SUCCESS, 0);
    ASSERT_TRUE(CONNX_DECODE_NEED_MORE > CONNX_DECODE_SUCCESS);
    ASSERT_TRUE(CONNX_DECODE_ERROR > CONNX_DECODE_NEED_MORE);
}

// LOGGING
TEST(CApiTest, log_set_get_level) {
    int original = connx_log_get_min_level();
    connx_log_set_min_level(CONNX_LOG_LEVEL_ERROR);
    ASSERT_EQ(connx_log_get_min_level(), CONNX_LOG_LEVEL_ERROR);
    connx_log_set_min_level(original);
}

TEST(CApiTest, log_set_callback_null_safe) { connx_log_set_callback(nullptr, nullptr); }

// ============================================================================
// CODEC OWNERSHIP
// ============================================================================

// set_codec transfers ownership from wrapper to options.
// codec_destroy after set_codec must not double-free.
TEST(CApiTest, ownership_codec_destroy_after_set_codec) {
    connx_codec_t* codec = connx_codec_new_delimiter('\n');
    connx_client_options_t* opts = connx_client_options_new();

    connx_client_options_set_codec(opts, codec); // codec ownership -> opts
    connx_codec_destroy(codec);                   // wrapper destroyed, codec NOT deleted
    connx_client_options_destroy(opts);           // deletes codec
}

// options_destroy after client_new must not double-free: codec moved to client.
TEST(CApiTest, ownership_options_destroy_after_client_new) {
    connx_client_handler_t* h =
        connx_client_handler_new(nullptr, nullptr, nullptr, nullptr, nullptr);
    connx_codec_t* codec = connx_codec_new_delimiter('\n');
    connx_client_options_t* opts = connx_client_options_new();

    connx_client_options_set_codec(opts, codec);
    connx_client_t* cli = connx_client_new(h, opts);
    ASSERT_TRUE(cli != nullptr);

    connx_client_options_destroy(opts); // codec already transferred to client
    connx_codec_destroy(codec);         // wrapper already empty
    connx_client_destroy(cli);          // deletes codec via ~ClientImpl
    connx_client_handler_destroy(h);
}

// Full ownership chain: wrapper -> opts -> client -> ~ClientImpl.
TEST(CApiTest, ownership_full_chain) {
    connx_client_handler_t* h =
        connx_client_handler_new(nullptr, nullptr, nullptr, nullptr, nullptr);
    connx_codec_t* codec = connx_codec_new_delimiter('\n');
    connx_client_options_t* opts = connx_client_options_new();

    connx_client_options_set_codec(opts, codec);
    connx_client_t* cli = connx_client_new(h, opts);
    ASSERT_TRUE(cli != nullptr);

    // All three destroy calls; only ~ClientImpl deletes the codec.
    connx_codec_destroy(codec);
    connx_client_options_destroy(opts);
    connx_client_destroy(cli);
    connx_client_handler_destroy(h);
}

// client_new failure: codec ownership stays with opts.
TEST(CApiTest, ownership_client_new_failure_codec_stays_with_opts) {
    connx_codec_t* codec = connx_codec_new_delimiter('\n');
    connx_client_options_t* opts = connx_client_options_new();
    connx_client_options_set_codec(opts, codec);

    // handler is NULL -> client_new must return NULL.
    connx_client_t* cli = connx_client_new(nullptr, opts);
    ASSERT_TRUE(cli == nullptr);

    // opts still owns codec.
    connx_client_options_destroy(opts); // deletes codec
    connx_codec_destroy(codec);         // wrapper already empty
}

// Calling set_codec twice replaces the old codec (old one destroyed).
TEST(CApiTest, ownership_set_codec_twice_replaces_codec) {
    connx_codec_t* c1 = connx_codec_new_delimiter('a');
    connx_codec_t* c2 = connx_codec_new_delimiter('b');
    connx_client_options_t* opts = connx_client_options_new();

    connx_client_options_set_codec(opts, c1); // opts takes c1, c1 wrapper emptied
    connx_client_options_set_codec(opts, c2); // opts deletes c1, takes c2

    connx_codec_destroy(c1); // wrapper already empty
    connx_codec_destroy(c2); // wrapper already empty
    connx_client_options_destroy(opts); // deletes c2
}

// options_destroy without set_codec: no codec to delete, must not crash.
TEST(CApiTest, ownership_options_destroy_without_codec) {
    connx_client_options_t* opts = connx_client_options_new();
    connx_client_options_destroy(opts); // opts.codec is nullptr
}

// codec_destroy without set_codec: wrapper owns codec, must be deleted.
TEST(CApiTest, ownership_codec_destroy_without_set_codec) {
    connx_codec_t* codec = connx_codec_new_delimiter('\n');
    connx_codec_destroy(codec); // wrapper owns codec, deleted here
}

// Repeat set_codec -> client_new -> all destroy. No crash on second iteration.
TEST(CApiTest, ownership_repeated_cycle) {
    for (int i = 0; i < 3; i++) {
        connx_client_handler_t* h =
            connx_client_handler_new(nullptr, nullptr, nullptr, nullptr, nullptr);
        connx_codec_t* codec = connx_codec_new_delimiter('\n');
        connx_client_options_t* opts = connx_client_options_new();

        connx_client_options_set_codec(opts, codec);
        connx_client_t* cli = connx_client_new(h, opts);
        ASSERT_TRUE(cli != nullptr);

        connx_codec_destroy(codec);
        connx_client_options_destroy(opts);
        connx_client_destroy(cli);
        connx_client_handler_destroy(h);
    }
}

// client_new failure with null opts: codec was never set, opts_destroy safe.
TEST(CApiTest, ownership_client_new_null_opts) {
    connx_client_handler_t* h =
        connx_client_handler_new(nullptr, nullptr, nullptr, nullptr, nullptr);
    connx_client_t* cli = connx_client_new(h, nullptr);
    ASSERT_TRUE(cli == nullptr);
    connx_client_handler_destroy(h);
}
