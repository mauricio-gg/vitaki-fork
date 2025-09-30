// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 protobuf utilities implementation

#include "pb_utils.h"

#include <nanopb/pb_decode.h>
#include <nanopb/pb_encode.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "../protobuf/takion.pb.h"
#include "../utils/logger.h"

// Protobuf utility functions for Remote Play protocol (vitaki-fork compatible)
// Note: Callback functions are now defined inline in pb_utils.h

// Utility function for XOR operations
void xor_bytes(uint8_t *dest, const uint8_t *src, size_t size) {
  for (size_t i = 0; i < size; i++) dest[i] ^= src[i];
}

// Real protobuf message encoding using nanopb
VitaRPS5Result encode_takion_message(const tkproto_TakionMessage *message,
                                     uint8_t *buffer, size_t buffer_size,
                                     size_t *encoded_size) {
  if (!message || !buffer || !encoded_size) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);

  if (!pb_encode(&stream, tkproto_TakionMessage_fields, message)) {
    log_error("Failed to encode TakionMessage: %s", PB_GET_ERROR(&stream));
    return VITARPS5_ERROR_UNKNOWN;
  }

  *encoded_size = stream.bytes_written;
  log_debug("Encoded TakionMessage successfully: %zu bytes", *encoded_size);
  return VITARPS5_SUCCESS;
}

// Real protobuf message decoding using nanopb
VitaRPS5Result decode_takion_message(const uint8_t *buffer, size_t buffer_size,
                                     tkproto_TakionMessage *message) {
  if (!buffer || !message) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  pb_istream_t stream = pb_istream_from_buffer(buffer, buffer_size);

  // Initialize message structure
  memset(message, 0, sizeof(*message));

  if (!pb_decode(&stream, tkproto_TakionMessage_fields, message)) {
    log_error("Failed to decode TakionMessage: %s", PB_GET_ERROR(&stream));
    return VITARPS5_ERROR_UNKNOWN;
  }

  log_debug("Decoded TakionMessage successfully: type=%d", message->type);
  return VITARPS5_SUCCESS;
}