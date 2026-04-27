/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_INCLUDE_C_H_
#define CONNX_INCLUDE_C_H_

#include <stddef.h>
#include <stdint.h>

#include "connx/export.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Opaque Types
// These are forward-declared structs to hide implementation details.
// Users should only interact with them via the provided API functions.
// ============================================================================

typedef struct connx_client_s connx_client_t;
typedef struct connx_client_handler_s connx_client_handler_t;
typedef struct connx_client_options_s connx_client_options_t;
typedef struct connx_codec_s connx_codec_t;

#define CONNX_STATE_DISCONNECTED 0
#define CONNX_STATE_CONNECTED    1

struct connx_metrics_s {
    int state; // CONNX_STATE_DISCONNECTED or CONNX_STATE_CONNECTED
    uint64_t bytes_sent;
    uint64_t bytes_received;
    size_t pending_bytes;
};
typedef struct connx_metrics_s connx_metrics_t;

typedef enum {
    CONNX_DECODE_SUCCESS = 0, // Successfully decoded a frame
    CONNX_DECODE_NEED_MORE,   // Insufficient data, wait for more
    CONNX_DECODE_ERROR        // Protocol error
} connx_decode_result_t;

/**
 * Decode protocol messages from a stream of data.
 *
 * This function processes a buffer of raw data received from the network,
 * decodes as many complete messages as possible, and reports how much data
 * was consumed and the length of the decoded message.
 *
 * @param userdata  User-provided pointer passed to `connx_codec_new_callback`.
 * @param data      Pointer to the input data buffer.
 * @param len       Length of the input data buffer in bytes.
 * @param consumed  Output parameter. On success, set to the number of bytes
 *                  consumed from the input buffer. This may be less than `len`
 *                  if the caller provided more data than needed for the
 *                  current message.
 *
 * @return DecodeResult indicating the outcome:
 *         - CONNX_DECODE_SUCCESS:  A complete message was decoded.
 *                                  `consumed` is valid. The caller may call
 *                                  the decode callbak again on the remaining
 *                                  data (if any).
 *         - CONNX_DECODE_NEED_MORE: Insufficient data to complete a message.
 *                                  `consumed` is set to 0. Wait for more data
 *                                  and call the decode callback again.
 *         - CONNX_DECODE_ERROR:    Protocol violation or invalid data. The
 *                                  connection should be closed.
 *
 * @note The caller is responsible for extracting the message body and
 *       advancing the input buffer by `consumed` bytes.
 *
 * @example
 *   size_t consumed = 0;
 *   connx_decode_result_t result = callback(userdata, buffer, buffer_len, &consumed);
 *   if (result == CONNX_DECODE_SUCCESS) {
 *       process_message(buffer, consumed);
 *       buffer += consumed;
 *       buffer_len -= consumed;
 *   }
 */
typedef connx_decode_result_t (*connx_decode_callback_t)(void* userdata, const void* data,
                                                         size_t len, size_t* consumed);

// ============================================================================
// Callback Function Types
// ============================================================================

CONNX_API connx_client_handler_t*
connx_client_handler_new(void* userdata, void (*on_connected)(void*),
                         void (*on_connect_failed)(void*, const char* reason),
                         void (*on_closed)(void*),
                         void (*on_message)(void*, const void* data, size_t size));
CONNX_API void connx_client_handler_destroy(connx_client_handler_t* handler);

// ============================================================================
// Codec
// ============================================================================
CONNX_API connx_codec_t* connx_codec_new_delimiter(char delimiter);
CONNX_API connx_codec_t* connx_codec_new_fixed_length(size_t frame_length);
CONNX_API connx_codec_t* connx_codec_new_length_field(size_t length_field_offset,
                                                      size_t length_field_length, size_t header_len,
                                                      size_t network_to_host);
CONNX_API connx_codec_t* connx_codec_new_callback(connx_decode_callback_t callback, void* userdata);
CONNX_API void connx_codec_destroy(connx_codec_t* codec);

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Creates a connx client options instance.
 *
 * @return A new client options instance, or NULL on failure.
 */
CONNX_API connx_client_options_t* connx_client_options_new();

/**
 * @brief Destroys a options instance
 *
 * @param options The options to destroy.
 */
CONNX_API void connx_client_options_destroy(connx_client_options_t* options);

/**
 * @brief Configures the protocol codec for the client.
 *
 * This must be called before `connx_client_connect`.
 *
 * @param options The options instance.
 * @param codec The codec instace.
 */
CONNX_API void connx_client_options_set_codec(connx_client_options_t* options,
                                              connx_codec_t* codec);

/**
 * @brief Sets the local address to bind to the before connecting
 *
 * Useful for multi-NIC machines where the server validates client IP against whitelist.
 * Pass NULL to let the OS auto-select the source address (default)
 * @param options The options instance
 * @param local_address The local IP address string (e.g., "192.168.1.100"), or NULL.
 */
CONNX_API void connx_client_options_set_local_address(connx_client_options_t* options,
                                                      const char* local_address);

/**
 * @brief Sets TCP_NODEALY option (default: enabled).
 */
CONNX_API void connx_client_options_set_tcp_nodelay(connx_client_options_t* options, int enabled);
/**
 * @brief Sets socket send buffer size (0 = OS default).
 */
CONNX_API void connx_client_options_set_send_buffer_size(connx_client_options_t* options, int size);
/**
 * @brief Sets socket receive buffer size (0 = OS default).
 */
CONNX_API void connx_client_options_set_recv_buffer_size(connx_client_options_t* options, int size);
/**
 * @brief Sets SO_LINGER timeout in seconds (-1 = disabled, default).
 */
CONNX_API void connx_client_options_set_linger(connx_client_options_t* options, int sec);
/**
 * @brief Sets connect timeout in milliseconds(0 = no timeout, default).
 */
CONNX_API void connx_client_options_set_connect_timeout(connx_client_options_t* options, int ms);

// ============================================================================
// Client Lifecycle
// ============================================================================
/**
 * @brief Creates a new connx client instance.
 *
 * @param handler Callback for connection state changes. Can't be NULL.
 * @param opts client options. Can't be NULL.
 * @return A new client instance, or NULL on failure.
 */
CONNX_API connx_client_t* connx_client_new(connx_client_handler_t* handler,
                                           connx_client_options_t* opts);

/**
 * @brief Destroys a client instance and frees all associated resources.
 *
 * This function will disconnect if the client is currently connected.
 *
 * @param client The client to destroy. Passing NULL is safe and results in a no-op.
 */
CONNX_API void connx_client_destroy(connx_client_t* client);

// ============================================================================
// Connection Management
// ============================================================================

/**
 * @brief Initiates a connection to a remote host and port.
 *
 * This is an asynchronous operation. The result will be reported via the state change callback.
 *
 * @param client The client instance.
 * @param host The hostname or IP:PORT address as a null-terminated string.
 * @return 0 on success (operation started), non-zero on immediate error (e.g., invalid arguments).
 */
CONNX_API int connx_client_connect(connx_client_t* client, const char* host);

/**
 * @brief Disconnects from the current remote host.
 *
 * This is an asynchronous operation.
 *
 * @param client The client instance.
 */
CONNX_API void connx_client_disconnect(connx_client_t* client);

/**
 * @brief Checks if the client is currently connected.
 *
 * @param client The client instance.
 * @return 1 if connected, 0 otherwise.
 */
CONNX_API int connx_client_is_connected(const connx_client_t* client);

// ============================================================================
// Data Transmission
// ============================================================================

/**
 * @brief Sends data to the connected remote host.
 *
 * This function queues the data for sending. It does not guarantee that the data has been sent
 * over the network upon return. The actual send completion can be tracked via a future API
 * (e.g., a write-complete callback).
 *
 * @param client The client instance.
 * @param data Pointer to the data to send.
 * @param len Length of the data in bytes.
 * @return A unique request ID for this send operation, or -1 on error.
 */
CONNX_API int64_t connx_client_send_buffer(connx_client_t* client, const void* data, size_t len);

// ============================================================================
// Observability
// ============================================================================

/**
 * @brief Retrieves the current metrics for the client.
 *
 * The returned `connx_metrics_t` struct is a snapshot of the internal counters.
 *
 * @param client The client instance.
 * @param metrics A pointer to a user-allocated `connx_metrics_t` struct to be filled.
 */
CONNX_API void connx_client_get_metrics(const connx_client_t* client, connx_metrics_t* metrics);

// ============================================================================
// Library Lifecycle
// ============================================================================

/**
 * @briefExplicitly initlializes the connx library.
 *
 * Optional. The library auto-initializes on first client creation.
 */
CONNX_API void connx_library_init();

/**
 * @briefExplicitly shutdown the connx library.
 *
 * Optional. Callings this while clients exist is undefined behavior.
 */
CONNX_API void connx_library_shutdown();

// ============================================================================
// Version
// ============================================================================

/**
 * @brief Returns the library version as a string (e.g. "0.1.0").
 */
CONNX_API const char* connx_version_string(void);

/**
 * @brief Returns the major version number.
 */
CONNX_API int connx_version_major(void);

/**
 * @brief Returns the minor version number.
 */
CONNX_API int connx_version_minor(void);

/**
 * @brief Returns the patch version number.
 */
CONNX_API int connx_version_patch(void);

// ============================================================================
// Logging
// ============================================================================

#define CONNX_LOG_LEVEL_TRACE 0
#define CONNX_LOG_LEVEL_DEBUG 1
#define CONNX_LOG_LEVEL_INFO  2
#define CONNX_LOG_LEVEL_WARN  3
#define CONNX_LOG_LEVEL_ERROR 4

typedef void (*connx_log_callback_t)(int level, int line, unsigned long threadid, const char* msg,
                                     void* userdata);

/**
 * @brief Gets the current minimum log level.
 */
CONNX_API int connx_log_get_min_level();

/**
 * @brief Sets the minimum log level. Messages below this level are discarded.
 */
CONNX_API void connx_log_set_min_level(int level);

/**
 * @brief Gets a custom log callbak. Pass NULL to restore default (stderr).
 */
CONNX_API void connx_log_set_callback(connx_log_callback_t callback, void* userdata);

#ifdef __cplusplus
}
#endif
#endif // CONNX_INCLUDE_C_H_
