/*
 * Copyright (c) 2026 connx contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef CONNX_INCLUDE_CODEC_H_
#define CONNX_INCLUDE_CODEC_H_

#include <stddef.h>

#include "connx/export.h"

namespace connx {

// Decoding result enumeration
enum class DecodeResult {
    kSuccess,      // Successfully decoded a frame
    kNeedMoreData, // Insufficient data, wait for more
    kError         // Protocol error
};

// Abstract base class for Protocol Codecs
class CONNX_API Codec {
public:
    virtual ~Codec();

    /**
     * Decode protocol messages from a stream of data.
     *
     * This function processes a buffer of raw data received from the network,
     * decodes as many complete messages as possible, and reports how much data
     * was consumed and the length of the decoded message.
     *
     * @param data      Pointer to the input data buffer.
     * @param len       Length of the input data buffer in bytes.
     * @param consumed  Output parameter. On success, set to the number of bytes
     *                  consumed from the input buffer. This may be less than `len`
     *                  if the caller provided more data than needed for the
     *                  current message.
     *
     * @return DecodeResult indicating the outcome:
     *         - kSuccess:      A complete message was decoded. `consumed` and
     *                          `msg_len` are valid. The caller may call decode()
     *                          again on the remaining data (if any).
     *         - kNeedMoreData: Insufficient data to complete a message.
     *                          `consumed` is set to 0. Wait for more data and
     *                          call decode() again.
     *         - kError:        Protocol violation or invalid data. The connection
     *                          should be closed.
     *
     * @note The caller is responsible for extracting the message body using
     *       `msg_len` and advancing the input buffer by `consumed` bytes.
     *       For zero-copy scenarios, consider passing an output buffer parameter.
     *
     * @example
     *   size_t consumed = 0;
     *   auto result = decoder.decode(buffer, buffer_len, &consumed);
     *   if (result == DecodeResult::kSuccess) {
     *       // Extract message of length `consumed`
     *       process_message(buffer, consumed);
     *       // Move to next message
     *       buffer += consumed;
     *       buffer_len -= consumed;
     *   }
     */
    virtual DecodeResult Decode(const char* data, size_t len, size_t* consumed_len) = 0;
};

} // namespace connx
#endif // CONNX_INCLUDE_CODEC_H_
