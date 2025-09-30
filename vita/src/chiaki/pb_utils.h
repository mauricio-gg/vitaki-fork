// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// VitaRPS5 adaptation of vitaki-fork pb_utils.h

#ifndef VITARPS5_PB_UTILS_H
#define VITARPS5_PB_UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../core/vitarps5.h"

// Real nanopb includes (ARM cross-compiled)
#include <nanopb/pb.h>
#include <nanopb/pb_decode.h>
#include <nanopb/pb_encode.h>

#include "../protobuf/takion.pb.h"

// Note: Message types and structures are now defined in takion.pb.h

#ifdef __cplusplus
extern "C" {
#endif

// Protobuf encoding/decoding utilities for VitaRPS5 (vitaki-fork compatible)

// Inline callback functions for protobuf encoding (vitaki-fork compatible)
static inline bool chiaki_pb_encode_string(pb_ostream_t *stream,
                                           const pb_field_t *field,
                                           void *const *arg) {
  char *str = *arg;

  if (!pb_encode_tag_for_field(stream, field)) return false;

  return pb_encode_string(stream, (uint8_t *)str, strlen(str));
}

typedef struct chiaki_pb_buf_t {
  size_t size;
  uint8_t *buf;
} ChiakiPBBuf;

static inline bool chiaki_pb_encode_buf(pb_ostream_t *stream,
                                        const pb_field_t *field,
                                        void *const *arg) {
  ChiakiPBBuf *buf = *arg;

  if (!pb_encode_tag_for_field(stream, field)) return false;

  return pb_encode_string(stream, buf->buf, buf->size);
}

// Critical: Zero encrypted key function for PS5 compatibility
static inline bool chiaki_pb_encode_zero_encrypted_key(pb_ostream_t *stream,
                                                       const pb_field_t *field,
                                                       void *const *arg) {
  if (!pb_encode_tag_for_field(stream, field)) return false;
  uint8_t data[] = {0, 0, 0, 0};
  return pb_encode_string(stream, data, sizeof(data));
}

typedef struct chiaki_pb_decode_buf_t {
  size_t max_size;
  size_t size;
  uint8_t *buf;
} ChiakiPBDecodeBuf;

static inline bool chiaki_pb_decode_buf(pb_istream_t *stream,
                                        const pb_field_t *field, void **arg) {
  ChiakiPBDecodeBuf *buf = *arg;
  if (stream->bytes_left > buf->max_size) {
    buf->size = 0;
    return false;
  }

  buf->size = stream->bytes_left;
  bool r = pb_read(stream, buf->buf, buf->size);
  if (!r) buf->size = 0;
  return r;
}

typedef struct chiaki_pb_decode_buf_alloc_t {
  size_t size;
  uint8_t *buf;
} ChiakiPBDecodeBufAlloc;

static inline bool chiaki_pb_decode_buf_alloc(pb_istream_t *stream,
                                              const pb_field_t *field,
                                              void **arg) {
  ChiakiPBDecodeBufAlloc *buf = *arg;
  buf->size = stream->bytes_left;
  buf->buf = malloc(buf->size);
  if (!buf->buf) return false;
  bool r = pb_read(stream, buf->buf, buf->size);
  if (!r) buf->size = 0;
  return r;
}

// Utility functions
void xor_bytes(uint8_t *dest, const uint8_t *src, size_t size);

// High-level protobuf message encoding/decoding for Remote Play protocol
VitaRPS5Result encode_takion_message(const tkproto_TakionMessage *message,
                                     uint8_t *buffer, size_t buffer_size,
                                     size_t *encoded_size);

VitaRPS5Result decode_takion_message(const uint8_t *buffer, size_t buffer_size,
                                     tkproto_TakionMessage *message);

// Additional utility for compatibility
#define LAUNCH_SPEC_JSON_BUF_SIZE 1024

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_PB_UTILS_H